#include <raylib.h>
#include <tileson.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/fmt/ostr.h>
#include <glm/ext/scalar_constants.hpp>
#include <list>
#include <gflags/gflags.h>

DEFINE_uint32(seed, 0, "Set random seed");
DEFINE_string(record_automation, "", "Record and save an automation list");
DEFINE_string(play_automation, "", "Play an automation list");
DEFINE_string(check_log, "", "Check that the specified string is contained in the log");

std::shared_ptr<spdlog::logger> raylibLog;
std::shared_ptr<spdlog::logger> contentLog;
std::shared_ptr<spdlog::logger> logicLog;
std::shared_ptr<spdlog::logger> gameSkeletonLog;

#ifdef WIN32
class Platform {
public:
	static std::filesystem::path getSaveFolder() {
		return std::filesystem::path(std::getenv("APPDATA")) / "diskiller";
	}

	enum {
		GAMEPAD_UP = GAMEPAD_BUTTON_LEFT_FACE_UP,
		GAMEPAD_RIGHT = GAMEPAD_BUTTON_LEFT_FACE_RIGHT,
		GAMEPAD_DOWN = GAMEPAD_BUTTON_LEFT_FACE_DOWN,
		GAMEPAD_LEFT = GAMEPAD_BUTTON_LEFT_FACE_LEFT,
		GAMEPAD_X = GAMEPAD_BUTTON_RIGHT_FACE_DOWN,
		GAMEPAD_O = GAMEPAD_BUTTON_RIGHT_FACE_RIGHT,
	};
};
#else
class Platform {
public:
	static std::filesystem::path getSaveFolder() {
		return std::filesystem::path(std::getenv("HOME")) / ".diskiller";
	}

	enum {
		GAMEPAD_UP = 13,
		GAMEPAD_RIGHT = 16,
		GAMEPAD_DOWN = 14,
		GAMEPAD_LEFT = 15,
		GAMEPAD_X = 0,
		GAMEPAD_O = 1,
	};
};
#endif

class SplashScreen;

struct Settings {
	float gravity = 9.81;
	float turnDelay = 1.0f;
	bool diskColliderDebugDraw = false;
	float diskColliderSize = 0.40f;
	bool rifleDebugDraw = false;
	float rifleSpeed = 1.0f;
	float rifleShootDelay = 0.5f;
	int rifleLookBackFrames = 2;
	int rifleLookForwardFrames = 2;
};

struct Savegame {
	struct BestScore {
		std::string mode;
		int score;
	};

	std::string lastSelectedGameMode;
	std::vector<BestScore> scores;
};

void from_json(const nlohmann::json& json, Settings& settings) {
	json.at("gravity").get_to(settings.gravity);
	json.at("turnDelay").get_to(settings.turnDelay);
	json.at("diskColliderDebugDraw").get_to(settings.diskColliderDebugDraw);
	json.at("diskColliderSize").get_to(settings.diskColliderSize);
	json.at("rifleDebugDraw").get_to(settings.rifleDebugDraw);
	json.at("rifleSpeed").get_to(settings.rifleSpeed);
	json.at("rifleShootDelay").get_to(settings.rifleShootDelay);
	json.at("rifleLookBackFrames").get_to(settings.rifleLookBackFrames);
	json.at("rifleLookForwardFrames").get_to(settings.rifleLookForwardFrames);
}

void from_json(const nlohmann::json& json, Savegame::BestScore& best_score) {
	json.at("mode").get_to(best_score.mode);
	json.at("score").get_to(best_score.score);
}

void from_json(const nlohmann::json& json, Savegame& savegame) {
	if (json.contains("lastSelectedGameMode")) {
		json.at("lastSelectedGameMode").get_to(savegame.lastSelectedGameMode);
	}
	json.at("scores").get_to(savegame.scores);
}

void to_json(nlohmann::json& json, const Savegame::BestScore& best_score) {
	json["mode"] = best_score.mode;
	json["score"] = best_score.score;
}

void to_json(nlohmann::json& json, const Savegame& savegame) {
	json["lastSelectedGameMode"] = savegame.lastSelectedGameMode;
	json["scores"] = savegame.scores;
}

struct Content {
	Texture2D sprites;
	std::unique_ptr<tson::Map> map;
	Font font;

	Sound reload;
	Sound shoot;
	Music menuMusic;

	Content() {
		contentLog->info("Loading sprites");
		sprites = LoadTexture("diskiller.png");

		contentLog->info("Loading map");
		tson::Tileson parser(std::unique_ptr<tson::IJson>(new tson::NlohmannJson));
		map = parser.parse("diskiller.tmj");

		contentLog->info("Loading font");
		font = LoadFontEx("cour.ttf", 96, nullptr, 0);

		reload = LoadSound("reload.mp3");
		shoot = LoadSound("shoot.mp3");
		menuMusic = LoadMusicStream("menu_music.mp3");
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

	virtual ~GameScreen() {}
	virtual std::optional<GameScreen*> update() = 0;
	virtual void render() = 0;
};

enum class SessionType {
	BestScore,
	Survival,
};

struct SessionDef {
	std::string gameModeName;
	SessionType type;
	int turnCount;
	int disksPerTurn;
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

namespace glm {
	std::ostream& operator<<(std::ostream& stream, const vec2& v) {
		return stream << "(" << v.x << "," << v.y << ")";
	}
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

		loadSavegame();

		for (int i = 0; i < gameModes.size(); ++i) {
			if (gameModes.at(i).gameModeName == savegame.lastSelectedGameMode) {
				modeSelection = i;
				break;
			}
		}
		PlayMusicStream(content.menuMusic);
	}

	SplashScreen(const Settings& _settings, Content& _content, const std::string& game_mode, const int your_score) : settings(_settings), content(_content) {
		gameSkeletonLog->info("Created SplashScreen from session end");

		loadSavegame();

		yourScore = your_score;
		subscreen = Subscreen::YourScore;
		if (updateBestScore(game_mode, your_score)) {
			saveSavegame();
		}

		for (int i = 0; i < gameModes.size(); ++i) {
			if (gameModes.at(i).gameModeName == savegame.lastSelectedGameMode) {
				modeSelection = i;
				break;
			}
		}
		PlayMusicStream(content.menuMusic);
	}

	~SplashScreen() {
		StopMusicStream(content.menuMusic);
	}

	std::optional<GameScreen*> update() override;

	void render() override {
		setCamera();

		BeginMode2D(camera);
		ClearBackground(Color{ content.map->getBackgroundColor().r, content.map->getBackgroundColor().g, content.map->getBackgroundColor().b, content.map->getBackgroundColor().a });
		if (subscreen == Subscreen::MainMenu) {
			DrawTextEx(content.font, "Diskiller", Vector2{ 4,4 }, 2, 0, BLACK);
			DrawTextEx(content.font, "Play", Vector2{ 3,8 }, 1, 0, BLACK);
			DrawTextEx(content.font, fmt::format("Mode: {}", gameModes.at(modeSelection).gameModeName).c_str(), Vector2{ 3,9 }, 1, 0, BLACK);
			DrawTextEx(content.font, "Records", Vector2{ 3,10 }, 1, 0, BLACK);
			DrawTextEx(content.font, "Exit", Vector2{ 3,11 }, 1, 0, BLACK);
			DrawTextEx(content.font, ">", Vector2{ 2, float(8 + menuSelection) }, 1, 0, BLACK);
			DrawTextEx(content.font, fmt::format("v{} {}", BUILD_VERSION, __DATE__).c_str(), Vector2{ 0, 15.5f }, 0.5, 0, BLACK);
		}
		else if (subscreen == Subscreen::Records) {
			for (int i = 0; i < gameModes.size(); ++i) {
				int score = 0;
				for (const auto& best_score : savegame.scores) {
					if (best_score.mode == gameModes.at(i).gameModeName) {
						score = best_score.score;
					}
				}

				DrawTextEx(content.font, gameModes.at(i).gameModeName.c_str(), Vector2{ 1, float(4 + i) }, 1, 0, BLACK);
				DrawTextEx(content.font, std::to_string(score).c_str(), Vector2{ 13, float(4 + i) }, 1, 0, BLACK);
			}
		}
		else if (subscreen == Subscreen::YourScore) {
			DrawTextEx(content.font, fmt::format("Your score is {}", yourScore).c_str(), Vector2{ 4, 5 }, 1, 0, BLACK);
		}
		EndMode2D();
	}

private:
	enum class Subscreen {
		MainMenu,
		Records,
		YourScore,
	};

	const Settings& settings;
	Content& content;
	Savegame savegame;

	Subscreen subscreen = Subscreen::MainMenu;
	int menuSelection = 0;
	int modeSelection = 0;
	int yourScore = 0;

	const std::array<SessionDef, 8> gameModes = {
	SessionDef { "Best of 10", SessionType::BestScore, 10, 1 },
	SessionDef { "Best of 25", SessionType::BestScore, 25, 1 },
	SessionDef { "Best of 100", SessionType::BestScore, 100, 1 },
	SessionDef { "Survival", SessionType::Survival, 0, 1 },
	SessionDef { "Expert Best of 10", SessionType::BestScore, 10, 3 },
	SessionDef { "Expert Best of 25", SessionType::BestScore, 25, 3 },
	SessionDef { "Expert Best of 100", SessionType::BestScore, 100, 3 },
	SessionDef { "Expert Survival", SessionType::Survival, 0, 3 },
	};

	void loadSavegame()
	{
		const std::filesystem::path savefile = Platform::getSaveFolder() / "savegame.json";
		logicLog->info("Loading savegame from file {}", savefile);

		if (!std::filesystem::exists(savefile)) {
			logicLog->warn("Savegame doesn't exist");
		}
		else {
			logicLog->info("Savegame exists, reading");

			std::ifstream stream(savefile);
			nlohmann::json json;
			stream >> json;
			savegame = json;
		}
	}

	void saveSavegame() {
		const std::filesystem::path savefile = Platform::getSaveFolder() / "savegame.json";
		logicLog->info("Saving savegame");

		std::ofstream stream(savefile);
		nlohmann::json json = savegame;
		stream << json;
	}

	bool updateBestScore(const std::string& mode, const int score) {
		bool present = false;
		for (auto& best_score : savegame.scores) {
			if (mode == best_score.mode) {
				present = true;

				if (score > best_score.score) {
					logicLog->info("Updating best score for mode {}", mode);
					best_score.score = score;
					return true;
				}
				else {
					logicLog->info("Existing score for mode {} is better", mode);
					return false;
				}

				break;
			}
		}

		if (!present) {
			logicLog->info("Best score not present for mode {}, adding", mode);

			Savegame::BestScore best_score;
			best_score.mode = mode;
			best_score.score = score;
			savegame.scores.push_back(best_score);

			return true;
		}

		return false;
	}
};

class Session : public GameScreen {
public:
	Session(const Settings& _settings, Content& _content, const SessionDef& session_def) : settings(_settings), content(_content), sessionDef(session_def) {
		gameSkeletonLog->info("Created Session, type = {}, turnCount = {}, disksPerTurn = {}", sessionDef.type, sessionDef.turnCount, sessionDef.disksPerTurn);
		memset(&camera, 0, sizeof(Camera2D));

		auto find_tile = [&content = content](const std::string& tile_class) -> tson::Tile* {
			for (tson::Tileset& tileset : content.map->getTilesets()) {
				for (tson::Tile& tile : tileset.getTiles()) {
					if (tile.getClassType() == tile_class) {
						return &tile;
					}
				}
			}

			contentLog->critical("Could not find tile for class {}", tile_class);
			return nullptr;
		};

		diskTile = find_tile("disk");
		explosionTile = find_tile("explosion");
		rifleTile = find_tile("rifle");
	}

	std::optional<GameScreen*> update() override {
		if (IsKeyPressed(KEY_BACKSPACE) || IsGamepadButtonPressed(0, Platform::GAMEPAD_O)) {
			return new SplashScreen(settings, content);
		}

		if (disks.empty() && GetTime() - lastDiskRemovedTime >= settings.turnDelay) {

			if (sessionDef.type == SessionType::BestScore && currentTurn == sessionDef.turnCount) {
				logicLog->info("Finished session with best score {}", successfulTurns);
				return new SplashScreen(settings, content, sessionDef.gameModeName, successfulTurns);
			}
			else if (sessionDef.type == SessionType::Survival && failedTurns > 0) {
				logicLog->info("Finished session with best score {}", successfulTurns);
				return new SplashScreen(settings, content, sessionDef.gameModeName, successfulTurns);
			}
			else {
				logicLog->info("Creating disks for turn {}", currentTurn + 1);

				for (int i = 0; i < sessionDef.disksPerTurn; ++i) {
					Disk disk;
					disk.velocity.x = randomFloat(-3, 3);
					disk.velocity.y = randomFloat(-10, -18);

					const float time_in_air = std::abs(disk.velocity.y / settings.gravity) * 2;
					const float traveled_distance = disk.velocity.x * time_in_air;

					disk.position.x = traveled_distance > 0 ? randomFloat(2, 14 - traveled_distance) : randomFloat(2 - traveled_distance, 14);
					disk.position.y = 16;
					disks.push_back(disk);

					logicLog->info("Disk spawned: velocity = {}, time_in_air = {}, traveled_distance = {}, position = {}", disk.velocity, time_in_air, traveled_distance, disk.position);
				}

				++currentTurn;
				hitDisks = 0;
				missedDisks = 0;
			}
		}

		{
			if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_RIGHT) || IsGamepadButtonDown(0, Platform::GAMEPAD_DOWN) || IsGamepadButtonDown(0, Platform::GAMEPAD_RIGHT)) {
				rifleAngle -= settings.rifleSpeed * GetFrameTime();
			}
			if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_LEFT) || IsGamepadButtonDown(0, Platform::GAMEPAD_UP) || IsGamepadButtonDown(0, Platform::GAMEPAD_LEFT)) {
				rifleAngle += settings.rifleSpeed * GetFrameTime();
			}
			rifleAngle = std::clamp<float>(rifleAngle, 0, glm::pi<float>() / 2);

			if (reloaded) {
				if (IsKeyPressed(KEY_SPACE) || IsGamepadButtonPressed(0, Platform::GAMEPAD_X)) {
					logicLog->info("Shooting");
					shootFrames = settings.rifleLookForwardFrames;
					reloaded = false;
					lastShotTime = GetTime();
					PlaySound(content.shoot);
				}
			}
			else {
				if (GetTime() - lastShotTime >= settings.rifleShootDelay) {
					logicLog->info("Reloading");
					reloaded = true;
					PlaySound(content.reload);
				}
			}
		}

		const bool projectile = shootFrames > 0;
		--shootFrames;

		{
			const glm::vec2 rifle_start(0.5f, 13.5f);
			const glm::vec2 rifle_end = rifle_start + glm::vec2(std::cos(rifleAngle), -std::sin(rifleAngle)) * 20.0f;
			int prev_disk_count = disks.size();

			for (auto iter = disks.begin(); iter != disks.end();) {
				iter->position += iter->velocity * GetFrameTime();
				iter->velocity += glm::vec2(0, settings.gravity) * GetFrameTime();

				iter->lookbackPositions.push_back(iter->position);
				while (iter->lookbackPositions.size() > settings.rifleLookBackFrames) {
					iter->lookbackPositions.pop_front();
				}

				bool hit = false;
				if (projectile) {
					for (const glm::vec2 lookback_position : iter->lookbackPositions) {
						hit = hit || collideLineCircle(lookback_position, settings.diskColliderSize, rifle_start, rifle_end);
					}
				}

				if (hit) {
					logicLog->info("Disk hit");

					Explosion explosion;
					explosion.position = iter->position;
					explosion.timeCreated = GetTime();
					explosion.animation = explosionTile->getAnimation();
					explosion.animation.reset();
					explosion.lifetime = 0;
					for (const tson::Frame& frame : explosion.animation.getFrames()) {
						explosion.lifetime += double(frame.getDuration()) / 1000.0f;
					}

					explosions.push_back(explosion);

					++hitDisks;
					iter = disks.erase(iter);
				}
				else if (iter->position.y > 16) {
					logicLog->info("Disk missed");

					++missedDisks;
					iter = disks.erase(iter);
				}
				else {
					++iter;
				}
			}

			if (prev_disk_count > 0 && disks.empty()) {
				if (missedDisks > 0) {
					logicLog->info("Turn finished with {} hit and {} missed disks, considered failed", hitDisks, missedDisks);
					++failedTurns;
				}
				else {
					logicLog->info("Turn finished with {} hit and {} missed disks, considered successful", hitDisks, missedDisks);
					++successfulTurns;
				}
				lastDiskRemovedTime = GetTime();
			}
		}

		for (auto iter = explosions.begin(); iter != explosions.end();) {
			if (GetTime() - iter->timeCreated >= iter->lifetime) {
				iter = explosions.erase(iter);
			}
			else {
				iter->animation.update(GetFrameTime() * 1000.0f);
				++iter;
			}
		}

		return std::nullopt;
	}

	void render() override {
		const float pixel_per_unit = std::min(float(GetScreenHeight()) / 16.0f, float(GetScreenWidth()) / 16.0f);
		memset(&camera, 0, sizeof(Camera2D));
		camera.zoom = pixel_per_unit;
		camera.offset.x = (GetScreenWidth() - pixel_per_unit * 16) / 2;
		camera.offset.y = (GetScreenHeight() - pixel_per_unit * 16) / 2;

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

		// Background
		for (tson::Layer& layer : content.map->getLayers()) {
			if (layer.getType() == tson::LayerType::TileLayer && layer.get<bool>("static")) {
				for (int i = 0; i < layer.getSize().x; ++i) {
					for (int j = 0; j < layer.getSize().y; ++j) {
						if (!layer.getTileData().count({ j,i })) {
							continue;
						}
						tson::Tile* tile = layer.getTileData().at({ j,i });
						draw_tile(tile, glm::ivec2{j, i});
					}
				}
			}
		}

		// Disks
		for (const Disk& disk : disks) {
			draw_tile(diskTile, disk.position - glm::vec2(0.5f));
			if (settings.diskColliderDebugDraw) {
				DrawCircleV(Vector2{ disk.position.x, disk.position.y }, settings.diskColliderSize, Color{ 255,0,0,192 });
			}
		}

		// Explosions
		for (const Explosion& explosion : explosions) {
			const uint32_t tile_id = explosion.animation.getCurrentTileId();
			const tson::Tile* explosion_frame = content.map->getTileMap().at(tile_id);
			draw_tile(explosion_frame, explosion.position - glm::vec2(0.5f));
		}

		// Rifle
		{
			const glm::vec2 rifle_position(0.5f, 13.5f);

			{
				const int rifle_width = rifleTile->get<int>("width");
				const int rifle_height = rifleTile->get<int>("height");

				Rectangle draw_rect;
				draw_rect.x = rifleTile->getDrawingRect().x / (float(rifleTile->getDrawingRect().width) / float(rifleTile->getTileset()->getTileSize().x));
				draw_rect.y = rifleTile->getDrawingRect().y / (float(rifleTile->getDrawingRect().height) / float(rifleTile->getTileset()->getTileSize().y));
				draw_rect.width = rifleTile->getTileset()->getTileSize().x * rifle_width;
				draw_rect.height = rifleTile->getTileset()->getTileSize().y * rifle_height;

				const Rectangle dest{ rifle_position.x, rifle_position.y, float(rifle_width), float(rifle_height) };
				const Vector2 origin{ 0.5f / rifle_width, 0.5f / rifle_height };
				const float rotation = glm::degrees(-rifleAngle);
				DrawTexturePro(content.sprites, draw_rect, dest, origin, rotation, WHITE);
			}

			if (settings.rifleDebugDraw)
			{
				const glm::vec2 rifle_end = rifle_position + glm::vec2(std::cos(rifleAngle), -std::sin(rifleAngle)) * 30.0f;
				DrawLineEx(Vector2{ rifle_position.x, rifle_position.y }, Vector2{ rifle_end.x, rifle_end.y }, 0.1f, YELLOW);
			}
		}

		// UI
		{
			std::string score;
			if (sessionDef.type == SessionType::BestScore) {
				score = fmt::format("Score {}, Turn {}/{}", successfulTurns, currentTurn, sessionDef.turnCount);
			}
			else {
				score = fmt::format("Score {}", successfulTurns);
			}
			DrawTextEx(content.font, score.c_str(), Vector2{ 0, 0 }, 1, 0, BLACK);
		}

		EndMode2D();
	}

private:
	struct Disk
	{
		glm::vec2 position;
		glm::vec2 velocity;
		std::list<glm::vec2> lookbackPositions;
	};

	struct Explosion
	{
		glm::vec2 position;
		tson::Animation animation;
		double timeCreated;
		double lifetime;
	};

	tson::Tile* diskTile = nullptr;
	tson::Tile* explosionTile = nullptr;
	tson::Tile* rifleTile = nullptr;

	const Settings& settings;
	Content& content;
	SessionDef sessionDef;

	Camera2D camera;

	std::list<Disk> disks;
	int currentTurn = 0;
	int hitDisks = 0;
	int missedDisks = 0;
	int successfulTurns = 0;
	int failedTurns = 0;
	double lastDiskRemovedTime = 0;

	float rifleAngle = 0;
	bool reloaded = true;
	double lastShotTime = 0;
	int shootFrames = 0;
	std::list<Explosion> explosions;

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
	if (subscreen == Subscreen::MainMenu) {
		if (IsKeyPressed(KEY_DOWN) || IsGamepadButtonPressed(0, Platform::GAMEPAD_DOWN)) {
			menuSelection = std::clamp(menuSelection + 1, 0, 3);
		}

		if (IsKeyPressed(KEY_UP) || IsGamepadButtonPressed(0, Platform::GAMEPAD_UP)) {
			menuSelection = std::clamp(menuSelection - 1, 0, 3);
		}

		if (menuSelection == 0) {
			if (IsKeyPressed(KEY_ENTER) || IsGamepadButtonPressed(0, Platform::GAMEPAD_X)) {
				savegame.lastSelectedGameMode = gameModes.at(modeSelection).gameModeName;
				saveSavegame();
				return new Session(settings, content, gameModes.at(modeSelection));
			}
		}
		else if (menuSelection == 1) {
			if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_ENTER) || IsGamepadButtonPressed(0, Platform::GAMEPAD_X)) {
				modeSelection = (modeSelection + 1) % gameModes.size();
			}

			if (IsKeyPressed(KEY_LEFT)) {
				modeSelection = (modeSelection - 1 + gameModes.size()) % gameModes.size();
			}
		}
		else if (menuSelection == 2) {
			if (IsKeyPressed(KEY_ENTER) || IsGamepadButtonPressed(0, Platform::GAMEPAD_X)) {
				subscreen = Subscreen::Records;
			}
		}
		else if (menuSelection == 3) {
			if (IsKeyPressed(KEY_ENTER) || IsGamepadButtonPressed(0, Platform::GAMEPAD_X)) {
				return nullptr;
			}
		}
	}
	else if (subscreen == Subscreen::Records || subscreen == Subscreen::YourScore) {
		if (IsKeyPressed(KEY_BACKSPACE) || IsGamepadButtonPressed(0, Platform::GAMEPAD_O)) {
			subscreen = Subscreen::MainMenu;
		}
	}

	UpdateMusicStream(content.menuMusic);

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

class Automation {
public:
	Automation(const std::filesystem::path& recording_path, const std::filesystem::path& playing_path) : recordingPath(recording_path), playingPath(playing_path) {
		memset(&automation, 0, sizeof(AutomationEventList));

		if (!recordingPath.empty()) {
			gameSkeletonLog->info("Starting to record automation");
			automation = LoadAutomationEventList(nullptr);
			SetAutomationEventList(&automation);
			StartAutomationEventRecording();
			SetAutomationEventBaseFrame(0);
		}
		else if (!playingPath.empty()) {
			gameSkeletonLog->info("Starting to play automation from file {}", playingPath);
			automation = LoadAutomationEventList(playingPath.string().c_str());
			SetAutomationEventBaseFrame(0);
		}
	}

	~Automation() {
		if (!recordingPath.empty()) {
			gameSkeletonLog->info("Saving recorded automation to file {}", recordingPath);
			StopAutomationEventRecording();
			ExportAutomationEventList(automation, recordingPath.string().c_str());
		}

		UnloadAutomationEventList(&automation);
		memset(&automation, 0, sizeof(AutomationEventList));
	}

	void beginFrame() {
		if (!playingPath.empty()) {
			while (automation_event < automation.count && automation_frame >= automation.events[automation_event].frame) {
				PlayAutomationEvent(automation.events[automation_event]);
				++automation_event;
			}
		}
	}

	void endFrame() {
		++automation_frame;
	}

private:
	AutomationEventList automation;
	const std::filesystem::path recordingPath;
	const std::filesystem::path playingPath;
	int automation_event = 0;
	int automation_frame = 0;
};

struct LogChecker : public spdlog::sinks::sink {
	std::unique_ptr<spdlog::formatter> formatter;
	const std::string stringToCheck;
	bool result = false;

	LogChecker(const std::string& string_to_check) : stringToCheck(string_to_check), formatter(new spdlog::pattern_formatter) {
	}

	void log(const spdlog::details::log_msg& msg) {
		spdlog::memory_buf_t formatted;
		formatter->format(msg, formatted);

		const bool found = std::search(formatted.begin(), formatted.end(), stringToCheck.begin(), stringToCheck.end()) != formatted.end();
		result = result || found;
	}

	void flush() override {}

	void set_pattern(const std::string& pattern) override {
		formatter.reset(new spdlog::pattern_formatter(pattern));
	}

	void set_formatter(std::unique_ptr<spdlog::formatter> sink_formatter) override {
		formatter = std::move(sink_formatter);
	}
};

int main(int argc, char* argv[]) {
	gflags::ParseCommandLineFlags(&argc, &argv, false);

	const std::filesystem::path save_folder = Platform::getSaveFolder();
	if (!std::filesystem::exists(save_folder)) {
		std::filesystem::create_directories(save_folder);
	}

	LogChecker* log_checker = nullptr;
	std::vector<spdlog::sink_ptr> sinks;
	sinks.emplace_back(new spdlog::sinks::stdout_color_sink_st);
	sinks.emplace_back(new spdlog::sinks::basic_file_sink_st((save_folder / "log.txt").string(), true));
	if (!FLAGS_check_log.empty()) {
		log_checker = new LogChecker(FLAGS_check_log);
		sinks.emplace_back(log_checker);
	}

	raylibLog.reset(new spdlog::logger("raylib", sinks.begin(), sinks.end()));
	contentLog.reset(new spdlog::logger("content", sinks.begin(), sinks.end()));
	logicLog.reset(new spdlog::logger("logic", sinks.begin(), sinks.end()));
	gameSkeletonLog.reset(new spdlog::logger("gameSkeleton", sinks.begin(), sinks.end()));

	raylibLog->set_level(spdlog::level::warn);

	SetConfigFlags(FLAG_WINDOW_RESIZABLE);
	SetTraceLogCallback(&traceLogCallback);
	SetTargetFPS(60);
	InitWindow(720, 720, "Diskiller");
	InitAudioDevice();

	std::srand(FLAGS_seed != 0 ? FLAGS_seed : std::time(nullptr));
	Automation automation(FLAGS_record_automation, FLAGS_play_automation);

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
		automation.beginFrame();

		if (IsKeyPressed(KEY_F5)) {
			settings = load_settings();
		}

		std::optional<GameScreen*> new_screen = game_screen->update();

		BeginDrawing();
		game_screen->render();
		EndDrawing();
		automation.endFrame();

		if (new_screen.has_value()) {
			if (new_screen == nullptr) {
				gameSkeletonLog->info("Quit detected");
				break;
			}
			else {
				gameSkeletonLog->info("New game screen detected");
				game_screen.reset();
				game_screen.reset(*new_screen);
			}
		}
	}

	CloseAudioDevice();
	CloseWindow();

	if (log_checker) {
		return log_checker->result ? 0 : 1;
	}

	return 0;
}
