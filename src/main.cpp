#include <raylib.h>
#include <tileson.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/fmt/ostr.h>
#include <glm/ext/scalar_constants.hpp>

std::shared_ptr<spdlog::sinks::sink> consoleSink;
std::shared_ptr<spdlog::logger> raylibLog;
std::shared_ptr<spdlog::logger> contentLog;
std::shared_ptr<spdlog::logger> logicLog;
std::shared_ptr<spdlog::logger> gameSkeletonLog;

class SplashScreen;

struct Settings {
	float gravity = 9.81;
	float diskDelay = 1.0f;
	float diskColliderSize = 0.40f;
	bool diskColliderDebugDraw = false;
	float rifleSpeed = 1.0f;
	float rifleLength = 1.5f;
	float explosionLifetime = 2.0f;
};

void from_json(const nlohmann::json& json, Settings& settings) {
	json.at("gravity").get_to(settings.gravity);
	json.at("diskDelay").get_to(settings.diskDelay);
	json.at("diskColliderSize").get_to(settings.diskColliderSize);
	json.at("diskColliderDebugDraw").get_to(settings.diskColliderDebugDraw);
	json.at("rifleSpeed").get_to(settings.rifleSpeed);
	json.at("rifleLength").get_to(settings.rifleLength);
	json.at("explosionLifetime").get_to(settings.explosionLifetime);
}

struct Content {
	Texture2D sprites;
	std::unique_ptr<tson::Map> map;
	Font font;

	Content() {
		contentLog->info("Loading sprites");
		sprites = LoadTexture("diskiller.png");

		contentLog->info("Loading map");
		tson::Tileson parser(std::unique_ptr<tson::IJson>(new tson::NlohmannJson));
		map = parser.parse("diskiller.tmj");

		contentLog->info("Loading font");
		font = LoadFontEx("cour.ttf", 96, nullptr, 0);
	}

	~Content() {
		contentLog->info("Unloading all");
		UnloadTexture(sprites);
		UnloadFont(font);
	}
};

static float randomFloat(const float min, const float max) {
	return float(rand() % RAND_MAX) / RAND_MAX * (max - min) + min;
}

class GameScreen {
public:

	virtual std::optional<GameScreen*> update() = 0;
	virtual void render() = 0;
};

enum class SessionType {
	BestScore,
	Survival,
};

struct SessionDef {
	SessionType type;
	int bestScoreMaxDisks;
};

std::ostream& operator<<(std::ostream& stream, const SessionType& session_type) {
	switch (session_type) {
	case SessionType::BestScore:
		stream << "BestScore";
	case SessionType::Survival:
		stream << "Survival";
	}

	return stream;
}

std::ostream& operator<<(std::ostream& stream, const glm::vec2& v) {
	return stream << "(" << v.x << "," << v.y << ")";
}

class UiScreen : public GameScreen {
protected:
	Camera2D camera;

	void setCamera() {
		const float pixel_per_unit = std::min(float(GetScreenHeight()) / 16.0f, float(GetScreenWidth()) / 16.0f);
		memset(&camera, 0, sizeof(Camera2D));
		camera.zoom = pixel_per_unit;
		camera.offset.x = (GetScreenWidth() - pixel_per_unit * 16) / 2;
		camera.offset.y = (GetScreenHeight() - pixel_per_unit * 16) / 2;
	}
};

class SplashScreen : public UiScreen {
public:
	SplashScreen(const Settings& _settings, Content& _content) : settings(_settings), content(_content) {
		gameSkeletonLog->info("Created SplashScreen");
	}

	std::optional<GameScreen*> update() override;

	void render() override {
		setCamera();

		BeginMode2D(camera);
		ClearBackground(Color{ content.map->getBackgroundColor().r, content.map->getBackgroundColor().g, content.map->getBackgroundColor().b, content.map->getBackgroundColor().a });
		DrawTextEx(content.font, "Diskiller", Vector2{ 4,4 }, 2, 0, BLACK);
		DrawTextEx(content.font, "Play", Vector2{ 5,8 }, 1, 0, BLACK);
		DrawTextEx(content.font, fmt::format("Mode: {}", gameModes.at(modeSelection).name).c_str(), Vector2{ 5,9 }, 1, 0, BLACK);
		DrawTextEx(content.font, "Records", Vector2{ 5,10 }, 1, 0, BLACK);
		DrawTextEx(content.font, "Credits", Vector2{ 5,11 }, 1, 0, BLACK);
		DrawTextEx(content.font, "Exit", Vector2{ 5,12 }, 1, 0, BLACK);
		DrawTextEx(content.font, ">", Vector2{ 4, float(8 + menuSelection) }, 1, 0, BLACK);
		EndMode2D();
	}

private:
	struct GameMode {
		std::string name;
		SessionDef sessionDef;
	};

	const std::array<GameMode, 5> gameModes = {
		GameMode { "Best of 10", SessionDef { SessionType::BestScore, 10 } },
		GameMode { "Best of 25", SessionDef { SessionType::BestScore, 25 } },
		GameMode { "Best of 50", SessionDef { SessionType::BestScore, 50 } },
		GameMode { "Best of 100", SessionDef { SessionType::BestScore, 100 } },
		GameMode { "Survival", SessionDef { SessionType::Survival } },
	};

	const Settings& settings;
	Content& content;
	int menuSelection = 0;
	int modeSelection = 0;
};

class Session : public GameScreen {
public:
	Session(const Settings& _settings, Content& _content, const SessionDef& session_def) : settings(_settings), content(_content), sessionDef(session_def) {
		gameSkeletonLog->info("Created Session, type = {}, bestScoreMaxDisks = {}", sessionDef.type, sessionDef.bestScoreMaxDisks);
		memset(&camera, 0, sizeof(Camera2D));
	}

	std::optional<GameScreen*> update() override {
		if (IsKeyPressed(KEY_BACKSPACE)) {
			return new SplashScreen(settings, content);
		}

		bool shot = false;

		{
			if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_RIGHT)) {
				rifle_angle -= settings.rifleSpeed * GetFrameTime();
			}
			if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_LEFT)) {
				rifle_angle += settings.rifleSpeed * GetFrameTime();
			}
			rifle_angle = std::clamp<float>(rifle_angle, 0, glm::pi<float>() / 2);

			if (IsKeyPressed(KEY_SPACE)) {
				logicLog->info("Shooting");
				shot = true;
			}
		}

		{
			const glm::vec2 rifle_start(0.5f, 13.5f);
			const glm::vec2 rifle_end = rifle_start + glm::vec2(std::cos(rifle_angle), -std::sin(rifle_angle)) * 20.0f;

			for (auto iter = disks.begin(); iter != disks.end();) {
				iter->position += iter->velocity * GetFrameTime();
				iter->velocity += glm::vec2(0, settings.gravity) * GetFrameTime();

				bool destroyed = false;

				if (shot && collideLineCircle(iter->position, settings.diskColliderSize, rifle_start, rifle_end)) {
					logicLog->info("Disk hit, hitDisks = {}", hitDisks + 1);

					Explosion explosion;
					explosion.position = iter->position;
					explosion.timeCreated = GetTime();
					explosions.push_back(explosion);

					hitDisks += 1;
					destroyed = true;
				}

				if (iter->position.y > 16) {
					logicLog->info("Disk disappeared");

					if (sessionDef.type == SessionType::Survival) {
						return new SplashScreen(settings, content);
					}
					else if (sessionDef.type == SessionType::BestScore) {
						if (totalDisks == sessionDef.bestScoreMaxDisks) {
							return new SplashScreen(settings, content);
						}
					}

					destroyed = true;
				}

				if (destroyed) {
					iter = disks.erase(iter);
					lastDiskTime = GetTime();
				}
				else {
					++iter;
				}
			}
		}

		for (auto iter = explosions.begin(); iter != explosions.end();) {
			if (GetTime() - iter->timeCreated > settings.explosionLifetime) {
				iter = explosions.erase(iter);
			}
			else {
				++iter;
			}
		}

		if (disks.empty() && GetTime() - lastDiskTime >= settings.diskDelay) {
			Disk disk;
			disk.velocity.x = randomFloat(-3, 3);
			disk.velocity.y = randomFloat(-10, -18);

			const float time_in_air = std::abs(disk.velocity.y / settings.gravity) * 2;
			const float traveled_distance = disk.velocity.x * time_in_air;

			disk.position.x = traveled_distance > 0 ? randomFloat(2, 14 - traveled_distance) : randomFloat(2 - traveled_distance, 14);
			disk.position.y = 16;

			disks.push_back(disk);
			totalDisks += 1;

			logicLog->info("Disk spawned: totalDisks = {}, velocity = {}, time_in_air = {}, traveled_distance = {}, position = {}", totalDisks, disk.velocity, time_in_air, traveled_distance, disk.position);
		}

		return std::nullopt;
	}

	void render() override {
		const float pixel_per_unit = std::min(float(GetScreenHeight()) / 16.0f, float(GetScreenWidth()) / 16.0f);
		memset(&camera, 0, sizeof(Camera2D));
		camera.zoom = pixel_per_unit;
		camera.offset.x = (GetScreenWidth() - pixel_per_unit * 16) / 2;
		camera.offset.y = (GetScreenHeight() - pixel_per_unit * 16) / 2;

		const auto& tiles = content.map->getTileMap();
		const tson::Tile* character_tile = tiles.at(Tile_Character + 1);
		const tson::Tile* rock_tile = tiles.at(Tile_Rock + 1);
		const tson::Tile* treebottom_tile = tiles.at(Tile_TreeBottom + 1);
		const tson::Tile* treetop_tile = tiles.at(Tile_TreeTop + 1);
		const tson::Tile* disk_tile = tiles.at(Tile_Disk + 1);
		const tson::Tile* explosion_tile = tiles.at(Tile_Explosion + 1);

		auto draw_tile = [&content = content](const tson::Tile* tile, const glm::vec2 position) {
			Rectangle draw_rect;
			draw_rect.x = tile->getDrawingRect().x / (float(tile->getDrawingRect().width) / float(tile->getTileset()->getTileSize().x));
			draw_rect.y = tile->getDrawingRect().y / (float(tile->getDrawingRect().height) / float(tile->getTileset()->getTileSize().y));
			draw_rect.width = tile->getTileset()->getTileSize().x;
			draw_rect.height = tile->getTileset()->getTileSize().y;
			DrawTexturePro(content.sprites, draw_rect, Rectangle{ position.x, position.y, 1, 1 }, Vector2{ 0,0 }, 0, WHITE);
		};

		ClearBackground(Color{ content.map->getBackgroundColor().r, content.map->getBackgroundColor().g, content.map->getBackgroundColor().b, content.map->getBackgroundColor().a });
		BeginMode2D(camera);
		draw_tile(character_tile, glm::vec2(0, 13));
		draw_tile(rock_tile, glm::vec2(0, 14));
		draw_tile(rock_tile, glm::vec2(0, 15));
		for (int i = 1; i < 16; ++i) {
			draw_tile(treebottom_tile, glm::vec2(i, 15));
			draw_tile(treetop_tile, glm::vec2(i, 14));
		}
		for (const Disk& disk : disks) {
			draw_tile(disk_tile, disk.position - glm::vec2(0.5f));
			if (settings.diskColliderDebugDraw) {
				DrawCircleV(Vector2{ disk.position.x, disk.position.y }, settings.diskColliderSize, Color{ 255,0,0,192 });
			}
		}
		for (const Explosion& explosion : explosions) {
			draw_tile(explosion_tile, explosion.position - glm::vec2(0.5f));
		}

		{
			const glm::vec2 rifle_start(0.5f, 13.5f);
			const glm::vec2 rifle_end = rifle_start + glm::vec2(std::cos(rifle_angle), -std::sin(rifle_angle)) * settings.rifleLength;
			DrawLineEx(Vector2{ rifle_start.x, rifle_start.y }, Vector2{ rifle_end.x, rifle_end.y }, 0.1f, YELLOW);
		}

		{
			std::string score;
			if (sessionDef.type == SessionType::BestScore) {
				score = fmt::format("Score {}, Disk {}/{}", hitDisks, totalDisks, sessionDef.bestScoreMaxDisks);
			}
			else {
				score = fmt::format("Score {}", hitDisks);
			}
			DrawTextEx(content.font, score.c_str(), Vector2{ 0, 0 }, 1, 0, BLACK);
		}

		EndMode2D();
	}

private:
	enum {
		Tile_Character = 30,
		Tile_Rock = 1,
		Tile_TreeBottom = 31,
		Tile_TreeTop = 23,
		Tile_Disk = 20,
		Tile_Explosion = 35,
	};

	struct Disk
	{
		glm::vec2 position;
		glm::vec2 velocity;
	};

	struct Explosion
	{
		glm::vec2 position;
		double timeCreated;
	};

	const Settings& settings;
	Content& content;
	SessionDef sessionDef;

	Camera2D camera;

	std::list<Disk> disks;
	std::list<Explosion> explosions;
	double lastDiskTime = 0;
	float rifle_angle = 0;
	int totalDisks = 0;
	int hitDisks = 0;

	static bool collideLineCircle(const glm::vec2& circle_center, const float circle_radius, const glm::vec2& line_start, const glm::vec2& line_end) {
		const float hyp = glm::length(circle_center - line_start);
		const float cath = glm::dot(circle_center - line_start, line_end - line_start) / glm::length(line_end - line_start);

		const float dist = std::sqrt(hyp * hyp - cath * cath);
		const bool result = hyp * hyp - cath * cath < circle_radius * circle_radius;

		logicLog->debug("collideLineCircle: dist = {}, result = {}", dist, result);

		return result;
	}
};

std::optional<GameScreen*> SplashScreen::update() {
	if (IsKeyPressed(KEY_DOWN)) {
		menuSelection = std::clamp(menuSelection + 1, 0, 4);
	}

	if (IsKeyPressed(KEY_UP)) {
		menuSelection = std::clamp(menuSelection - 1, 0, 4);
	}

	if (IsKeyPressed(KEY_ENTER)) {
		switch (menuSelection)
		{
		case 0:
			return new Session(settings, content, gameModes.at(modeSelection).sessionDef);

		case 1:
			modeSelection = (modeSelection + 1) % gameModes.size();
			break;

		case 4:
			exit(0);

		default:
			break;
		}
	}

	return std::nullopt;
}

static void traceLogCallback(int logLevel, const char* text, va_list args) {
	static std::array<char, 4096> buffer;
	vsnprintf(buffer.data(), buffer.size(), text, args);

	switch (logLevel) {
	case LOG_ALL:
	case LOG_TRACE:
		raylibLog->trace(buffer.data());
		break;

	case LOG_DEBUG:
		raylibLog->debug(buffer.data());
		break;

	case LOG_INFO:
		raylibLog->info(buffer.data());
		break;

	case LOG_WARNING:
		raylibLog->warn(buffer.data());
		break;

	case LOG_ERROR:
		raylibLog->error(buffer.data());
		break;

	case LOG_FATAL:
		raylibLog->critical(buffer.data());
		break;

	case LOG_NONE:
		break;
	}
}

int main() {
	consoleSink.reset(new spdlog::sinks::stdout_color_sink_st);
	raylibLog.reset(new spdlog::logger("raylib", consoleSink));
	contentLog.reset(new spdlog::logger("content", consoleSink));
	logicLog.reset(new spdlog::logger("logic", consoleSink));
	gameSkeletonLog.reset(new spdlog::logger("gameSkeleton", consoleSink));

	raylibLog->set_level(spdlog::level::warn);

	SetConfigFlags(FLAG_WINDOW_RESIZABLE);
	SetTraceLogCallback(&traceLogCallback);
	InitWindow(720, 720, "Diskiller");

	auto load_settings = []() -> Settings
	{
		gameSkeletonLog->info("Loading settings");

		std::ifstream stream("settings.json");

		nlohmann::json json;
		stream >> json;

		Settings settings;
		settings = json;

		return settings;
	};

	Settings settings = load_settings();
	Content content;

	std::unique_ptr<GameScreen> game_screen;
	game_screen.reset(new SplashScreen(settings, content));

	while (!WindowShouldClose()) {
		if (IsKeyPressed(KEY_F5)) {
			settings = load_settings();
		}

		std::optional<GameScreen*> new_screen = game_screen->update();

		BeginDrawing();
		game_screen->render();
		EndDrawing();

		if (new_screen.has_value()) {
			gameSkeletonLog->info("New game screen detected");
			game_screen.reset();
			game_screen.reset(*new_screen);
		}
	}

	CloseWindow();

	return 0;
}
