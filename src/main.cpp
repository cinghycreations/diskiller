#include <raylib.h>
#include <tileson.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

std::shared_ptr<spdlog::sinks::sink> consoleSink;
std::shared_ptr<spdlog::logger> raylibLog;
std::shared_ptr<spdlog::logger> logicLog;

struct Settings {
	float diskDelay = 1.0f;
	float gravity = 9.81;
};

struct Content {
	Texture2D sprites;
	std::unique_ptr<tson::Map> map;

	Content() {
		sprites = LoadTexture("diskiller.png");

		tson::Tileson parser(std::unique_ptr<tson::IJson>(new tson::NlohmannJson));
		map = parser.parse("diskiller.tmj");
	}

	~Content() {
		UnloadTexture(sprites);
	}
};

static float randomFloat(const float min, const float max) {
	return float(rand() % RAND_MAX) / RAND_MAX * (max - min) + min;
}

class Session {
public:

	Session(const Settings& _settings, Content& _content) : settings(_settings), content(_content) {
		memset(&camera, 0, sizeof(Camera2D));
	}

	void update() {
		if (disks.empty() && GetTime() - lastDiskTime >= settings.diskDelay) {
			Disk disk;
			disk.velocity.x = randomFloat(-3, 3);
			disk.velocity.y = randomFloat(-10, -18);

			const float time_in_air = std::abs(disk.velocity.y / settings.gravity) * 2;
			const float traveled_distance = disk.velocity.x * time_in_air;

			disk.position.x = traveled_distance > 0 ? randomFloat(2, 14 - traveled_distance) : randomFloat(2 - traveled_distance, 14);
			disk.position.y = 16;

			logicLog->debug("Spawned disk: velocity.x = {}, velocity.y = {}, time_in_air = {}, traveled_distance = {}, position.x = {}, position.y = {}", disk.velocity.x, disk.velocity.y, time_in_air, traveled_distance, disk.position.x, disk.position.y);
			disks.push_back(disk);
		}

		for (auto iter = disks.begin(); iter != disks.end();) {
			iter->position += iter->velocity * GetFrameTime();
			iter->velocity += glm::vec2(0, settings.gravity) * GetFrameTime();
			if (iter->position.y > 16) {
				iter = disks.erase(iter);
				lastDiskTime = GetTime();
			}
			else {
				++iter;
			}
		}
	}

	void render() {
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
			draw_tile(disk_tile, disk.position);
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
	};

	struct Disk
	{
		glm::vec2 position;
		glm::vec2 velocity;
	};

	const Settings& settings;
	Content& content;
	Camera2D camera;

	std::list<Disk> disks;
	double lastDiskTime = 0;
};

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
	consoleSink.reset(new spdlog::sinks::stdout_color_sink_mt);
	raylibLog.reset(new spdlog::logger("raylib", consoleSink));
	logicLog.reset(new spdlog::logger("logic", consoleSink));
	logicLog->set_level(spdlog::level::debug);

	SetTraceLogCallback(&traceLogCallback);
	InitWindow(720, 720, "Diskiller");

	Settings settings;
	Content content;
	Session session(settings, content);

	while (!WindowShouldClose()) {
		BeginDrawing();

		session.update();
		session.render();

		EndDrawing();
	}

	CloseWindow();

	return 0;
}
