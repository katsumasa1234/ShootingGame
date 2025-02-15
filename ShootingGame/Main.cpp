# include <Siv3D.hpp>

const Polygon unitPolygon{std::initializer_list<Vec2>{Vec2{-10, -30}, Vec2{10, -30}, Vec2{10, -10}, Vec2{30, -10}, Vec2{30, 30}, Vec2{-30, 30}, Vec2{-30, -10}, Vec2{-10, -10}}};
const Polygon bulletPolygon = Rect{ -5, -10, 5, 10 }.asPolygon();
const P2Filter friendFilter{.categoryBits = 0b0000'0000'0000'0001, .maskBits = 0b1111'1111'1111'1101};
const P2Filter friendBulletFilter{ .categoryBits = 0b0000'0000'0000'0010, .maskBits = 0b1111'1111'1111'1110 };
const P2Filter enemyFilter{ .categoryBits = 0b0000'0000'0000'0100, .maskBits = 0b1111'1111'1111'0111 };
const P2Filter enemyBulletFilter{ .categoryBits = 0b0000'0000'0000'1000, .maskBits = 0b1111'1111'1111'1011 };

struct Unit;
struct Bullet;
struct UnitPlayer;
struct UnitEnemy;

HashTable<P2BodyID, Bullet> bullets;
HashTable<P2BodyID, UnitEnemy> enemies;
Array<UnitPlayer> player;

Font* titleFont;
Font* buttonFont;
Font* uiFont;
Effect* effect;

P2World world{ 0.0 };

int score = 0;
int nextEvent = 3000;
int gameStartTime = 0;
int gameEndTime = 0;
int defeating = 0;

Vec2 GetPos(float rateX, float rateY, int offsetX = 0, int offsetY = 0) {
	int x = Scene::Width() * rateX + offsetX;
	int y = Scene::Height() * rateY + offsetY;
	return Vec2{ x, y };
}

enum GameState {
	Title, Game, Result, EXIT
};
GameState gameState = GameState::Title;

struct HitEffect : IEffect {
	const int effectCount = 10;
	const int effectCountRange = 3;
	const double angleRange = Math::Pi / 4;

	Vec2 velocity;
	Vec2 position;
	Array<Vec2> effects;
	HitEffect(Vec2 position, Vec2 velocity) {
		this->velocity = velocity;
		this->position = position;

		srand(Time::GetMillisecSinceEpoch());
		int count = rand() % effectCountRange + effectCount - effectCountRange / 2.0;
		for (int i = 0; i < count; i++) {
			double v = (rand() % 1001) / 1000.0;
			double angle = fmod(rand(), angleRange) - angleRange / 2;
			effects << (velocity * v).rotated(angle);
		}
	}

	bool update(double t) override {
		for (int i = 0; i < effects.size(); i++) {
			Circle{ position + effects[i] * t, 2 }.draw();
		}

		return (t < 0.2);
	}
};

struct DeathEffect : IEffect {
	const int effectCount = 10;
	const int effectCountRange = 3;
	const double angleRange = 2 * Math::Pi;
	const int speed = 400;
	const int baseSpeed = 50;
	const double baseScale = 5;
	const double scaleRange = 10;
	const double registance = 0.1;

	Color baseColor;
	Color borderColor;
	Vec2 position;
	Array<Vec2> effectsVelocity;
	Array<double> effectsScale;
	DeathEffect(Vec2 position, Color baseColor, Color borderColor, double scale) {
		this->position = position;
		this->baseColor = baseColor;
		this->borderColor = borderColor;

		srand(Time::GetMillisecSinceEpoch());
		int count = rand() % effectCountRange + effectCount - effectCountRange / 2.0;
		for (int i = 0; i < count; i++) {
			double v = (rand() % 1001) / 1000.0;
			double angle = fmod(rand(), angleRange) - angleRange / 2;
			effectsVelocity << (Vec2::Up() * v * speed + Vec2::Up().withLength(baseSpeed)).rotated(angle);
			effectsScale << scale * fmod(rand(), scaleRange) + baseScale;
		}
	}

	bool update(double t) override {
		for (int i = 0; i < effectsVelocity.size(); i++) {
			Circle circle{ position + effectsVelocity[i] * t * Math::Pow(registance, t), effectsScale[i]};
			circle.draw(baseColor);
			circle.drawFrame(1, borderColor);
		}

		return (t < 0.4);
	}
};

struct Object {
	P2World world;
	P2Body body;
	double thickness;
	Color baseColor;
	Color borderColor;
	Polygon polygon;
	double scale;
	bool isReleased = false;

	Object(P2World world, Polygon polygon, P2BodyType type, Color baseColor, Color borderColor, double thickness, Vec2 position, P2Filter filter, double scale = 1) {
		polygon.scale(scale);
		this->world = world; this->polygon = polygon; this->thickness = thickness; this->baseColor = baseColor; this->borderColor = borderColor; this->scale = scale;
		body = world.createPolygon(type, position, polygon, {}, filter);
	}

	void draw() {
		body.draw(baseColor);
		body.drawFrame(thickness, borderColor);
	}

	Vec2 getCenter() {
		return body.getPos();
	}

	void virtual release() {
		body.release();
		isReleased = true;
	}

	void virtual update() {}
};

struct Bullet : Object {
	int bornTime;
	int damage;
	Bullet(P2World world, Vec2 position, Vec2 velocity, P2Filter filter, Color baseColor, Color borderColor, int damage = 10, double scale = 1)
		: Object(world, bulletPolygon, P2BodyType::Dynamic, baseColor, borderColor, 1, position, filter, scale) {
		this->damage = damage;

		body.setBullet(true);
		body.setVelocity(velocity);
		body.setAngle(velocity.getAngle());

		bornTime = Time::GetSec();
	}

	void release() override {
		bullets.erase(body.id());
		body.release();
		isReleased = true;
	}

	void update() override {
		if (Time::GetSec() - bornTime >= 10) release();
	}

	int getDamage() {
		return damage;
	}
};

struct Unit : Object {
	int fireCooldown = 100;
	int reloadTime = 3000;
	int maxBullets = 30;
	int maxHP;
	int bulletSpeed;
	int bulletDamage;
	double bulletScale;
	double maxSpeed;
	double acceleration;
	double angleSpeed;
	int hp;
	int bulletsCount = maxBullets;
	int previousFire = 0;
	int previousReload = 0;
	P2Filter bulletFilter;

	Unit(P2World world, Color baseColor, Color borderColor, double thickness, Vec2 position, P2Filter filter, P2Filter bulletFilter, int maxHP, double maxSpeed, double acceleration, double angleSpeed = 10, int fireCooldown = 100, int reloadTime = 3000, double scale = 1, int maxBullets = 30, int bulletSpeed = 5000, int bulletDamage = 10, double bulletScale = 1)
		: Object(world, unitPolygon, P2BodyType::Dynamic, baseColor, borderColor, thickness, position, filter, scale) {
		this->maxSpeed = maxSpeed;
		this->maxHP = maxHP;
		this->acceleration = acceleration;
		this->angleSpeed = angleSpeed;
		this->hp = maxHP;
		this->fireCooldown = fireCooldown;
		this->reloadTime = reloadTime;
		this->maxBullets = maxBullets;
		this->bulletSpeed = bulletSpeed;
		this->bulletDamage = bulletDamage;
		this->bulletScale = bulletScale;
		this->bulletFilter = bulletFilter;
	}

	void move(Vec2 orientation) {
		if (orientation.isZero()) body.applyForce(body.getVelocity().withLength(-acceleration));
		else body.applyForce(orientation.withLength(acceleration));

		body.setVelocity(body.getVelocity().withLength(Min(body.getVelocity().length(), maxSpeed)));
	}

	void facing(Vec2 pos) {
		Vec2 orientation = pos - getCenter();
		body.setAngle(std::fmod(body.getAngle() - 2 * Math::Pi, 2 * Math::Pi));
		double difference = orientation.getAngle() - body.getAngle();
		double angle = difference > Math::Pi ? difference - 2 * Math::Pi : difference;
		body.setAngularVelocity(angle / Abs(angle) * Min(angleSpeed, angle * angle * angleSpeed));
	}

	void fire() {
		if (Time::GetMillisec() - previousReload >= reloadTime && previousReload != 0) {
			previousReload = 0;
			bulletsCount = maxBullets;
		}
		if (bulletsCount == 0) reload();
		if (bulletsCount > 0 && Time::GetMillisec() - previousFire >= fireCooldown && previousReload == 0) {
			Bullet bullet{ world, getCenter(), Vec2::Up().withLength(bulletSpeed).rotated(body.getAngle()), bulletFilter, baseColor, borderColor, bulletDamage, bulletScale };
			bullets.emplace(bullet.body.id(), bullet);
			bulletsCount--;
			previousFire = Time::GetMillisec();
			body.applyForce(bullet.body.getMass() * bullet.body.getVelocity() * -100);
		}
	}

	void reload() {
		if (previousReload == 0) previousReload = Time::GetMillisec();
	}

	void damage(int value) {
		hp = Max(0, hp - value);
		if (hp == 0) death();
	}

	void virtual death() {
		effect->add<DeathEffect>(getCenter(), baseColor, borderColor, scale);
		release();
	}

	void release() override {
		enemies.erase(body.id());
		body.release();
		isReleased = true;
	}

	void virtual control() {}

	String getBulletsStr() {
		if (Time::GetMillisec() - previousReload >= reloadTime && previousReload != 0) {
			previousReload = 0;
			bulletsCount = maxBullets;
		}
		return previousReload != 0 ? U"Reloading" : ToString(bulletsCount);
	}
};

struct UnitPlayer : Unit {
	UnitPlayer(P2World world, Vec2 position, int maxHP, int maxSpeed, double acceleration)
		: Unit(world, Palette::Blue, Palette::White, 3, position, friendFilter, friendBulletFilter, maxHP, maxSpeed, acceleration) {
	}

	void control() override {
		Vec2 vec{ 0, 0 };
		if (KeyW.pressed()) vec.y = -1;
		if (KeyA.pressed()) vec.x = -1;
		if (KeyS.pressed()) vec.y = 1;
		if (KeyD.pressed()) vec.x = 1;
		move(vec);
		facing(Cursor::PosF());

		if (KeySpace.pressed() || MouseL.pressed()) fire();
		if (KeyR.pressed()) reload();
	}

	void release() override {
		isReleased = true;
		body.release();
	}
};

struct EnemyController {
	enum Mode { Normal, Size };

	int maxHp = 50;
	double maxSpeed = 500;
	double acceleration = 500;
	double angleSpeed = 10;
	int fireCooldown = 100;
	int reloadTime = 3000;
	double scale = 1;
	int maxBullets = 30;
	int bulletSpeed = 5000;
	int bulletDamage = 10;
	double bulletScale = 1;

	double distance = 300;
	Mode mode = Mode::Normal;


	Vec2 getMoveVector(UnitPlayer* player, Vec2 me) {
		if (!me.intersects(Scene::Rect())) return GetPos(0.5, 0.5) - me;

		if (mode == Mode::Normal) {
			Vec2 vec = player->getCenter() - me;
			vec.setLength(Max(vec.length() - distance, 0.0));
			return vec;
		}

		return Vec2::Zero();
	}

	Vec2 getFacing(UnitPlayer* player, Vec2 me) {
		if (mode == Mode::Normal) {
			return player->getCenter();
		}

		return Vec2::Zero();
	}

	bool getIsFire(UnitPlayer* player, Vec2 me) {
		if (mode == Mode::Normal) {
			return true;
		}

		return false;
	}
};

struct UnitEnemy : Unit {
	EnemyController controller;
	UnitPlayer* player;

	UnitEnemy(P2World world, Vec2 position, EnemyController controller, UnitPlayer* player)
		: Unit(world, Palette::Red, Palette::White, 3, position, enemyFilter, enemyBulletFilter, controller.maxHp, controller.maxSpeed, controller.acceleration, controller.angleSpeed, controller.fireCooldown, controller.reloadTime, controller.scale, controller.maxBullets, controller.bulletSpeed, controller.bulletDamage, controller.bulletScale) {
		this->controller = controller;
		this->player = player;
	}

	void control() override {
		move(controller.getMoveVector(player, getCenter()));
		facing(controller.getFacing(player, getCenter()));
		if (controller.getIsFire(player, getCenter())) fire();
	}

	void death() override {
		effect->add<DeathEffect>(getCenter(), baseColor, borderColor, scale);
		release();

		score += scale * 1000;
		defeating++;
	}
};















void InitContent() {
	titleFont = new Font{ FontMethod::MSDF, 128, Typeface::Bold };
	buttonFont = new Font{ FontMethod::MSDF, 64, Typeface::Bold };
	uiFont = new Font{ FontMethod::MSDF, 32 };
	effect = new Effect();
}

void ReleaseContent() {
	delete titleFont;
	delete buttonFont;
	delete uiFont;
	delete effect;
}

bool ButtonAt(Vec2 pos, Size size, String text, Color foreground = Palette::White, Color background = Palette::Deepskyblue) {
	int x = pos.x - size.x / 2.0;
	int y = pos.y - size.y / 2.0;
	Rect rect = Rect{ x, y, size.x, size.y };
	if (rect.mouseOver()) rect.drawShadow(Vec2{ 3, 3 }, 3, 0,Palette::Skyblue);
	rect.draw(background);
	(*buttonFont)(text).drawAt(pos, foreground);
	
	return rect.leftClicked();
}

void InitGame() {
	player.clear();
	for (auto&& [id, e] : enemies) e.release();
	enemies.clear();
	for (auto&& [id, b] : bullets) b.release();
	bullets.clear();
	player << UnitPlayer{ world, GetPos(0.5, 0.5), 100, 500, 500 };
	srand(Time::GetMillisecSinceEpoch());
	score = 0;
	gameStartTime = Time::GetSec();
	nextEvent = Time::GetMillisec() + 3000;
	gameEndTime = 0;
	defeating = 0;
}

void CollisionEvent() {
	for (auto&& [pair, collision] : world.getCollisions()) {
		if (!(bullets.contains(pair.a) || bullets.contains(pair.b))) continue;

		int damage = 0;

		if (bullets.contains(pair.a)) {
			effect->add<HitEffect>(collision.contact(0).point, bullets.at(pair.a).body.getVelocity());
			damage = bullets.at(pair.a).getDamage();
			bullets.at(pair.a).release();
		}
		if (bullets.contains(pair.b)) {
			effect->add<HitEffect>(collision.contact(0).point, bullets.at(pair.b).body.getVelocity());
			damage = bullets.at(pair.b).getDamage();
			bullets.at(pair.b).release();
		}

		if (enemies.contains(pair.a)) {
			enemies.at(pair.a).damage(damage);
		}
		if (enemies.contains(pair.b)) {
			enemies.at(pair.b).damage(damage);
		}

		if (player[0].body.id() == pair.a || player[0].body.id() == pair.b) {
			player[0].damage(damage);
		}
	}
}

Vec2 GetRandomOutsidePoint(const Rect& rect, double margin = 100.0)
{
	int edge = Random(3);

	switch (edge)
	{
	case 0:
		return Vec2(Random(rect.x - margin, rect.x + rect.w + margin), rect.y - margin);
	case 1:
		return Vec2(Random(rect.x - margin, rect.x + rect.w + margin), rect.y + rect.h + margin);
	case 2:
		return Vec2(rect.x - margin, Random(rect.y - margin, rect.y + rect.h + margin));
	case 3:
		return Vec2(rect.x + rect.w + margin, Random(rect.y - margin, rect.y + rect.h + margin));
	}

	return Vec2(0, 0);
}

void SpawnRandomEnemy() {
	EnemyController ec;
	Vec2 position = GetRandomOutsidePoint(Scene::Rect());

	ec.scale = fmod(rand(), 1000) / 500 + 0.5;
	ec.bulletScale = ec.scale;
	ec.bulletDamage *= ec.scale;
	ec.maxSpeed /= ec.scale * ec.scale;
	ec.acceleration /= ec.scale * ec.scale;
	ec.angleSpeed /= ec.scale;
	ec.bulletSpeed /= ec.scale;
	ec.fireCooldown *= ec.scale * ec.scale;
	ec.maxBullets /= ec.scale;
	ec.maxHp *= ec.scale;
	ec.reloadTime = fmod(rand(), ec.reloadTime) + ec.reloadTime / 2.0;
	ec.distance = fmod(rand(), 1000);

	UnitEnemy enemy{world, position, ec, &player[0]};
	enemies.emplace(enemy.body.id(), enemy);
}

void ShowTitle() {
	(*titleFont)(U"Shooting Game").drawAt(GetPos(0.5, 0.3, 3, 3), Palette::Gray);
	(*titleFont)(U"Shooting Game").drawAt(GetPos(0.5, 0.3), Palette::White);

	if (ButtonAt(GetPos(0.5, 0.5), Size{ 300, 100 }, U"START")) {
		InitGame();
		gameState = GameState::Game;
	}
	if (ButtonAt(GetPos(0.5, 0.65), Size{ 300, 100 }, U"EXIT")) {
		if (System::MessageBoxOKCancel(U"終了", U"ゲームを終了しますか？") == MessageBoxResult::OK) {
			gameState = GameState::EXIT;
		}
	}
}

void ShowGame() {
	if (nextEvent <= Time::GetMillisec()) {
		nextEvent = Time::GetMillisec() + rand() % 10000 + 3000;
		int count = rand() % 2 + 1;
		for (int i = 0; i < count; i++) SpawnRandomEnemy();
	}


	for (auto&& [id, b] : bullets) {
		b.draw();
		b.update();
	}
	for (auto&& [id, u] : enemies) {
		u.control();
		u.draw();
	}
	if (!player[0].isReleased) {
		player[0].control();
		player[0].draw();
	}

	world.update();
	CollisionEvent();
	effect->update();



	(*uiFont)(U"HP:").drawAt(GetPos(0.5, 0.95, -500, 0));
	Rect{-450, -15, 900, 30}.movedBy(GetPos(0.5, 0.95).asPoint()).draw(Palette::White);
	Rect{ -450, -15, (int)(player[0].hp / (float)player[0].maxHP * 900), 30 }.movedBy(GetPos(0.5, 0.95).asPoint()).draw(Palette::Green);

	(*uiFont)(U"Bullets: ", player[0].getBulletsStr()).drawAt(GetPos(0.9, 0.95));

	(*uiFont)(U"{:02d}:{:02d}"_fmt((Time::GetSec() - gameStartTime) / 60, (Time::GetSec() - gameStartTime) % 60)).drawAt(GetPos(0.5, 0.05));


	if (gameEndTime == 0 && player[0].isReleased) {
		gameEndTime = Time::GetSec();
		score += 100 * (gameEndTime - gameStartTime);
	}
	if (Time::GetSec() - gameEndTime >= 2 && gameEndTime != 0) {
		(*titleFont)(U"Game Over").drawAt(GetPos(0.5, 0.5, 3, 3), Palette::Gray);
		(*titleFont)(U"Game Over").drawAt(GetPos(0.5, 0.5), Palette::White);
	}
	if (Time::GetSec() - gameEndTime >= 6 && gameEndTime != 0) {
		gameState = GameState::Result;
	}
}

void ShowResult() {
	(*titleFont)(U"Result").drawAt(GetPos(0.5, 0.3, 3, 3), Palette::Gray);
	(*titleFont)(U"Result").drawAt(GetPos(0.5, 0.3), Palette::White);

	(*uiFont)(U"Score: {}"_fmt(score)).drawAt(GetPos(0.5, 0.45), Palette::White);
	(*uiFont)(U"Survival Time: {:02d}:{:02d}"_fmt((gameEndTime - gameStartTime) / 60, (gameEndTime - gameStartTime) % 60)).drawAt(GetPos(0.5, 0.5), Palette::White);
	(*uiFont)(U"Defeating: {}"_fmt(defeating)).drawAt(GetPos(0.5, 0.55), Palette::White);

	if (ButtonAt(GetPos(0.5, 0.7), Size{ 300, 100 }, U"To Title")) gameState = GameState::Title;
}

void Main()
{
	InitContent();

	System::SetTerminationTriggers(UserAction::NoAction);

	Window::SetStyle(WindowStyle::Frameless);
	Window::SetTitle(U"Shooting Game");
	Window::Maximize();
	
	Scene::SetBackground(Palette::Black);

	while (System::Update())
	{	
		if (gameState == GameState::Title) ShowTitle();
		else if (gameState == GameState::Game) ShowGame();
		else if (gameState == GameState::Result) ShowResult();
		else if (gameState == GameState::EXIT) break;
	}

	ReleaseContent();
}
