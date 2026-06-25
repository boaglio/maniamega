// ____________________________
// ██▀▀█▀▀██▀▀▀▀▀▀▀█▀▀█        │   ManiaMega
// ██  ▀  █▄  ▀██▄ ▀ ▄█ ▄▀▀ █  │   A Megamania-style fixed shooter for MSX2
// █  █ █  ▀▀  ▄█  █  █ ▀▄█ █▄ │   Built with MSXgl
// ▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀────────┘
//─────────────────────────────────────────────────────────────────────────────
// Plays like Activision's Megamania (1982):
//   - ship moves in all directions in a lower band and WRAPS horizontally
//   - rapid multi-shot upward; enemies do NOT shoot back
//   - enemies fly as a weaving serpentine STREAM that wraps and drifts down
//   - a constantly draining ENERGY bar is the real threat; clearing a wave pays
//     an energy bonus; run out of energy (or get hit) and you lose a ship
//   - one object type per wave; descending game-over tune
// (Our own title screen is kept.)
//─────────────────────────────────────────────────────────────────────────────

//=============================================================================
// INCLUDES
//=============================================================================
#include "msxgl.h"
#include "psg.h"
#include "font/font_mgl_sample6.h"

//=============================================================================
// DEFINES
//=============================================================================

// Sound effect lengths (frames). We emulate the Atari 2600 TIA character on the PSG.
#define SHOT_LEN			9		// buzzy descending "zap" (tone+noise blend)
#define BOOM_LEN			18		// noise rumble sweeping bright->dark + tone body

// Sprite pattern slots (8-byte units; a 16x16 sprite uses 4). shape = slot*4.
#define SHAPE_SHIP			(0 * 4)
#define SHAPE_ENEMY_A		(1 * 4)
#define SHAPE_ENEMY_B		(2 * 4)
#define SHAPE_BULLET		(3 * 4)
#define ENEMY_ANIM_MASK		0x08	// toggle enemy frame every 8 game frames

// Sprite planes [0:31]
#define MAX_BULLETS			3
#define ENEMY_COUNT			6		// enemies visible on screen at once
#define SPR_SHIP			0
#define SPR_BULLET_FIRST	1		// planes 1 .. MAX_BULLETS
#define SPR_ENEMY_FIRST		(SPR_BULLET_FIRST + MAX_BULLETS)	// planes 4 .. 9
#define SPR_LIFE_FIRST		(SPR_ENEMY_FIRST + ENEMY_COUNT)	// reserve-ship icons
#define MAX_LIFE_ICONS		5

// Playfield (SCREEN5 = 256x212; sprites 16px)
#define SCREEN_W			256
#define SPR_OFF_Y			213		// y >= 213 hides a sprite
#define SHIP_Y_MIN			150		// ship is confined to a lower band...
#define SHIP_Y_MAX			184		// ...but can move freely within it
#define SHIP_SPEED			2
#define BULLET_SPEED		6
#define FIRE_COOLDOWN		7		// frames between shots while fire is held
#define ENEMY_TOP			36		// where a fresh wave's baseline starts

// Wave / difficulty
#define WAVE_QUOTA			15		// enemies to destroy to clear a wave

// Energy bar
#define ENERGY_MAX			224		// 1 unit == 1 pixel of bar width
#define ENERGY_BAR_X		16
#define ENERGY_BAR_Y		200
#define ENERGY_BAR_H		7

#define LIVES_START			3

//=============================================================================
// READ-ONLY DATA
//=============================================================================

// Ship + per-level enemy patterns, adapted from the C64 Megamania project.
#include "mm_sprites.h"

// Bullet (thin vertical bolt)
const u8 g_SprBullet[] = {
	0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
	0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
	0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
	0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
};

// One color per enemy type/level (cycles with NUM_ENEMY_TYPES)
const u8 g_EnemyColor[NUM_ENEMY_TYPES] = {
	COLOR_LIGHT_GREEN, COLOR_LIGHT_YELLOW, COLOR_CYAN, COLOR_LIGHT_RED, COLOR_MAGENTA,
};

// One period of a sine wave, amplitude +/-31, 32 steps (for the weaving stream)
const i8 g_Sin32[32] = {
	  0,  6, 12, 17, 22, 26, 29, 30, 31, 30, 29, 26, 22, 17, 12,  6,
	  0, -6,-12,-17,-22,-26,-29,-30,-31,-30,-29,-26,-22,-17,-12, -6,
};

// 5x7 block letters for the big title (each row is 5 bits, MSB = leftmost pixel).
// Only the letters used by "MANIAMEGA" are defined: M A N I E G.
const u8 g_BigFont[6][7] = {
	{ 0x11,0x1B,0x15,0x11,0x11,0x11,0x11 },	// 0: M
	{ 0x0E,0x11,0x11,0x1F,0x11,0x11,0x11 },	// 1: A
	{ 0x11,0x19,0x15,0x13,0x11,0x11,0x11 },	// 2: N
	{ 0x1F,0x04,0x04,0x04,0x04,0x04,0x1F },	// 3: I
	{ 0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F },	// 4: E
	{ 0x0F,0x10,0x10,0x13,0x11,0x11,0x0F },	// 5: G
};
// Letter indices spelling MANIAMEGA
const u8 g_TitleLetters[9] = { 0,1,2,3,1,0,4,5,1 };

//=============================================================================
// VARIABLES
//=============================================================================

// Player
i16  g_ShipX, g_ShipY;
u8   g_FireTimer;

// Bullets
i16  g_BulletX[MAX_BULLETS], g_BulletY[MAX_BULLETS];
bool g_BulletLive[MAX_BULLETS];

// Enemy stream
i16  g_EnemyX[ENEMY_COUNT];
i16  g_EnemyY[ENEMY_COUNT];		// derived from X via the sine track each frame
bool g_EnemyLive[ENEMY_COUNT];
u8   g_WaveLeft;				// enemies still to destroy this wave (incl. on screen)
i16  g_SpawnX;					// x used for the next (re)spawn, to spread the stream

// Per-wave motion parameters
i8   g_WaveDir;					// +1 / -1 horizontal scroll direction
u8   g_WaveSpeed;				// horizontal pixels per frame
u8   g_WaveAmp;					// vertical weave amplitude (px)
u8   g_WaveShift;				// sine wavelength control (x >> shift)
i16  g_BaseY;					// stream baseline (drifts down slowly)
u8   g_DescentTimer;

// Progress
u16  g_Score;
u8   g_Lives;
u8   g_Level;					// 0-based; type = g_Level % NUM_ENEMY_TYPES

// Energy
i16  g_Energy;
u8   g_DrainTimer, g_DrainRate;
i16  g_EnergyDrawn;				// last bar width drawn (to avoid redundant fills)

// Sound effect timers
u8   g_SfxShot, g_SfxBoom;

//=============================================================================
// SOUND  (Atari-2600 TIA-flavored, on the MSX PSG)
//=============================================================================

void SoundInit()
{
	u8 c;
	for (c = 0; c < 3; ++c)
	{
		PSG_SetVolume(c, 0);
		PSG_EnableTone(c, FALSE);
		PSG_EnableNoise(c, FALSE);
	}
	PSG_Apply();
	g_SfxShot = 0;
	g_SfxBoom = 0;
}

// Advance shot + explosion one frame and flush (PSG access is INDIRECT).
void SoundUpdate()
{
	if (g_SfxShot)								// shot: buzzy descending zap
	{
		u8 age = SHOT_LEN - g_SfxShot;
		PSG_SetTone(PSG_CHANNEL_A, 110 + age * age * 9);
		PSG_SetVolume(PSG_CHANNEL_A, (g_SfxShot > 12) ? 12 : g_SfxShot);
	}
	if (g_SfxBoom)								// explosion: noise sweep + tone body
	{
		u8 age = BOOM_LEN - g_SfxBoom;
		u8 vol = (g_SfxBoom > 15) ? 15 : g_SfxBoom;
		PSG_SetVolume(PSG_CHANNEL_C, vol);
		PSG_SetTone(PSG_CHANNEL_B, 900 + age * 110);
		PSG_SetVolume(PSG_CHANNEL_B, (vol > 3) ? vol - 3 : 0);
	}
	if (g_SfxBoom)
		PSG_SetNoise(3 + (BOOM_LEN - g_SfxBoom));	// bright -> dark TIA rumble
	else if (g_SfxShot)
		PSG_SetNoise(10);

	PSG_EnableTone (PSG_CHANNEL_A, g_SfxShot != 0);
	PSG_EnableNoise(PSG_CHANNEL_A, g_SfxShot != 0);
	PSG_EnableTone (PSG_CHANNEL_B, g_SfxBoom != 0);
	PSG_EnableNoise(PSG_CHANNEL_C, g_SfxBoom != 0);

	if (g_SfxShot && --g_SfxShot == 0)
		PSG_SetVolume(PSG_CHANNEL_A, 0);
	if (g_SfxBoom && --g_SfxBoom == 0)
	{
		PSG_SetVolume(PSG_CHANNEL_B, 0);
		PSG_SetVolume(PSG_CHANNEL_C, 0);
	}
	PSG_Apply();
}

// Blocking descending "game over" tune on channel A.
void PlayGameOverTune()
{
	u8 n, h;
	g_SfxShot = g_SfxBoom = 0;
	PSG_EnableTone(PSG_CHANNEL_A, TRUE);
	PSG_EnableNoise(PSG_CHANNEL_A, FALSE);
	for (n = 0; n < 16; ++n)
	{
		PSG_SetTone(PSG_CHANNEL_A, 300 + n * 90);	// pitch falls
		PSG_SetVolume(PSG_CHANNEL_A, 13 - (n >> 1));
		PSG_Apply();
		for (h = 0; h < 5; ++h) Halt();
	}
	PSG_SetVolume(PSG_CHANNEL_A, 0);
	PSG_EnableTone(PSG_CHANNEL_A, FALSE);
	PSG_Apply();
}

// Blocking rising "wave cleared" arpeggio on channel A.
void PlayWaveJingle()
{
	u8 n, h;
	PSG_EnableTone(PSG_CHANNEL_A, TRUE);
	for (n = 0; n < 8; ++n)
	{
		PSG_SetTone(PSG_CHANNEL_A, 700 - n * 70);	// pitch rises
		PSG_SetVolume(PSG_CHANNEL_A, 12);
		PSG_Apply();
		for (h = 0; h < 3; ++h) Halt();
	}
	PSG_SetVolume(PSG_CHANNEL_A, 0);
	PSG_EnableTone(PSG_CHANNEL_A, FALSE);
	PSG_Apply();
}

//=============================================================================
// HELPERS
//=============================================================================

bool IsFire()
{
	if (Keyboard_IsKeyPressed(KEY_SPACE))
		return TRUE;
	return (Joystick_Read(JOY_PORT_1) & JOY_INPUT_TRIGGER_A) == 0;
}

// Read 8-way movement (joystick + cursor keys) and the fire button.
void ReadInput(i8* dx, i8* dy, bool* fire)
{
	u8 jd = Joystick_GetDirection(JOY_PORT_1);
	*dx = 0; *dy = 0;

	if (jd == JOY_INPUT_DIR_LEFT  || jd == JOY_INPUT_DIR_UP_LEFT  || jd == JOY_INPUT_DIR_DOWN_LEFT)  *dx = -1;
	if (jd == JOY_INPUT_DIR_RIGHT || jd == JOY_INPUT_DIR_UP_RIGHT || jd == JOY_INPUT_DIR_DOWN_RIGHT) *dx = 1;
	if (jd == JOY_INPUT_DIR_UP    || jd == JOY_INPUT_DIR_UP_LEFT  || jd == JOY_INPUT_DIR_UP_RIGHT)   *dy = -1;
	if (jd == JOY_INPUT_DIR_DOWN  || jd == JOY_INPUT_DIR_DOWN_LEFT|| jd == JOY_INPUT_DIR_DOWN_RIGHT) *dy = 1;

	if (Keyboard_IsKeyPressed(KEY_LEFT))  *dx = -1;
	if (Keyboard_IsKeyPressed(KEY_RIGHT)) *dx = 1;
	if (Keyboard_IsKeyPressed(KEY_UP))    *dy = -1;
	if (Keyboard_IsKeyPressed(KEY_DOWN))  *dy = 1;

	*fire = IsFire();
}

// Axis-aligned overlap of two 16x16 sprites.
bool Hit16(i16 ax, i16 ay, i16 bx, i16 by)
{
	i16 dx = ax - bx, dy = ay - by;
	if (dx < 0) dx = -dx;
	if (dy < 0) dy = -dy;
	return (dx < 16) && (dy < 16);
}

// Vertical position of an enemy from its X (rides a fixed sine "track").
i16 EnemyYFor(i16 x)
{
	i8 s = g_Sin32[(x >> g_WaveShift) & 31];
	return g_BaseY + (((i16)s * g_WaveAmp) >> 5);
}

void SpawnEnemy(u8 slot)
{
	g_EnemyLive[slot] = TRUE;
	g_EnemyX[slot] = g_SpawnX;
	g_SpawnX += SCREEN_W / ENEMY_COUNT + 7;		// spread successive spawns out
	if (g_SpawnX >= SCREEN_W) g_SpawnX -= SCREEN_W;
}

void Pause(u8 frames)
{
	while (frames--)
	{
		Halt();
		SoundUpdate();
	}
}

//-----------------------------------------------------------------------------
// HUD + energy bar
void DrawHud()
{
	u8 k;

	// Score: just the digits, like the original
	Print_SetColor(COLOR_WHITE, COLOR_BLACK);
	Print_SetPosition(8, 0);
	Print_DrawInt(g_Score);
	Print_DrawText("    ");

	// Remaining ships shown as little ship icons (no text)
	for (k = 0; k < MAX_LIFE_ICONS; ++k)
	{
		if (k < g_Lives)
			VDP_SetSprite(SPR_LIFE_FIRST + k, 232 - k * 18, 0, SHAPE_SHIP);
		else
			VDP_SetSpritePositionY(SPR_LIFE_FIRST + k, SPR_OFF_Y);
	}
}

void DrawEnergyBar(bool force)
{
	i16 w = g_Energy;
	if (w < 0) w = 0;
	if (!force && w == g_EnergyDrawn)
		return;
	g_EnergyDrawn = w;

	u8 col = (w > 150) ? COLOR_LIGHT_GREEN : (w > 70) ? COLOR_LIGHT_YELLOW : COLOR_LIGHT_RED;
	if (w > 0)
		VDP_CommandHMMV(ENERGY_BAR_X, ENERGY_BAR_Y, w, ENERGY_BAR_H, col);
	if (w < ENERGY_MAX)
		VDP_CommandHMMV(ENERGY_BAR_X + w, ENERGY_BAR_Y, ENERGY_MAX - w, ENERGY_BAR_H, COLOR_DARK_BLUE);
}

//-----------------------------------------------------------------------------
// Begin the current level's wave: patterns, color, motion params, enemies.
void LoadWave()
{
	u8 i;
	u8 t = g_Level % NUM_ENEMY_TYPES;

	VDP_LoadSpritePattern(g_EnemyA[t], SHAPE_ENEMY_A, 4);
	VDP_LoadSpritePattern(g_EnemyB[t], SHAPE_ENEMY_B, 4);
	for (i = 0; i < ENEMY_COUNT; ++i)
		VDP_SetSpriteUniColor(SPR_ENEMY_FIRST + i, g_EnemyColor[t]);

	g_WaveDir   = (g_Level & 1) ? -1 : 1;			// alternate scroll direction
	g_WaveSpeed = 1 + (g_Level / 3);  if (g_WaveSpeed > 3) g_WaveSpeed = 3;
	g_WaveAmp   = 22 + (g_Level % 3) * 8;			// 22 / 30 / 38 px weave
	g_WaveShift = (g_Level & 1) ? 2 : 3;			// tighter / wider weave
	g_BaseY     = ENEMY_TOP;
	g_DescentTimer = 0;

	g_WaveLeft = WAVE_QUOTA;
	g_SpawnX = 0;
	for (i = 0; i < ENEMY_COUNT; ++i)
		SpawnEnemy(i);

	g_Energy = ENERGY_MAX;
	g_DrainRate = 4;  if (g_Level < 6) g_DrainRate = 4 - (g_Level / 2);	// faster later
	if (g_DrainRate < 1) g_DrainRate = 1;
	g_DrainTimer = 0;
	DrawEnergyBar(TRUE);
}

void ResetPlayer()
{
	u8 i;
	g_ShipX = (SCREEN_W - 16) / 2;
	g_ShipY = SHIP_Y_MAX;
	g_FireTimer = 0;
	for (i = 0; i < MAX_BULLETS; ++i)
		g_BulletLive[i] = FALSE;
}

//=============================================================================
// SCREENS
//=============================================================================

// Draw "MANIAMEGA" as big block letters using VDP rectangle fills.
void DrawBigTitle()
{
	#define CELL	5					// pixels per letter cell
	#define LETW	(5 * CELL)			// letter width  (25)
	#define LGAP	2					// gap between letters
	u8  n, row, col;
	const u8* g;
	u16 x = (SCREEN_W - (9 * LETW + 8 * LGAP)) / 2;	// center the 9 letters
	u16 y = 80;

	for (n = 0; n < 9; ++n)
	{
		g = g_BigFont[g_TitleLetters[n]];
		for (row = 0; row < 7; ++row)
			for (col = 0; col < 5; ++col)
				if (g[row] & (0x10 >> col))
					VDP_CommandHMMV(x + col * CELL, y + row * CELL, CELL, CELL, COLOR_WHITE);
		x += LETW + LGAP;
	}
}

void TitleScreen()
{
	VDP_HideAllSprites();
	VDP_ClearVRAM();
	DrawBigTitle();

	while (IsFire()) Halt();
	while (!IsFire()) Halt();
}

void GameOverScreen()
{
	VDP_HideAllSprites();
	PlayGameOverTune();
	VDP_ClearVRAM();

	Print_SetColor(COLOR_LIGHT_RED, COLOR_BLACK);
	Print_DrawTextAt(96, 70, "G A M E   O V E R");
	Print_SetColor(COLOR_WHITE, COLOR_BLACK);
	Print_SetPosition(86, 100);
	Print_DrawText("SCORE ");
	Print_DrawInt(g_Score);
	Print_DrawTextAt(74, 150, "PRESS FIRE TO CONTINUE");

	Pause(60);
	while (IsFire()) Halt();
	while (!IsFire()) Halt();
}

//-----------------------------------------------------------------------------
// Award the remaining-energy bonus with a little tally animation.
// Convert the remaining energy into bonus points: the bar drains into the score
// with a string of blips. No on-screen text (the original game has none here).
void WaveCleared()
{
	PlayWaveJingle();
	while (g_Energy > 0)
	{
		g_Energy -= 4;  if (g_Energy < 0) g_Energy = 0;
		g_Score += 1;
		DrawEnergyBar(FALSE);
		DrawHud();
		g_SfxShot = 3;				// tally blips
		Pause(2);
	}
	Pause(30);
}

//=============================================================================
// GAMEPLAY
//=============================================================================

void PlayGame()
{
	u8 i, j;
	i8 dx, dy;
	bool fire;
	u8 frame = 0;

	g_Score = 0;
	g_Lives = LIVES_START;
	g_Level = 0;

	VDP_ClearVRAM();
	VDP_LoadSpritePattern(g_SprShip,   SHAPE_SHIP,   4);
	VDP_LoadSpritePattern(g_SprBullet, SHAPE_BULLET, 4);
	VDP_SetSpriteUniColor(SPR_SHIP, COLOR_WHITE);
	for (i = 0; i < MAX_BULLETS; ++i)
		VDP_SetSpriteUniColor(SPR_BULLET_FIRST + i, COLOR_LIGHT_YELLOW);
	for (i = 0; i < MAX_LIFE_ICONS; ++i)
		VDP_SetSpriteUniColor(SPR_LIFE_FIRST + i, COLOR_WHITE);

	LoadWave();
	ResetPlayer();
	DrawHud();

	while (TRUE)
	{
		Halt();
		frame++;
		bool died = FALSE;
		u8 enemyShape = (frame & ENEMY_ANIM_MASK) ? SHAPE_ENEMY_B : SHAPE_ENEMY_A;

		// ---- Input: 8-way move, ship wraps horizontally, stays in lower band
		ReadInput(&dx, &dy, &fire);
		g_ShipX += dx * SHIP_SPEED;
		g_ShipY += dy * SHIP_SPEED;
		if (g_ShipX < 0)             g_ShipX += SCREEN_W;	// horizontal wrap
		if (g_ShipX >= SCREEN_W)     g_ShipX -= SCREEN_W;
		if (g_ShipY < SHIP_Y_MIN)    g_ShipY = SHIP_Y_MIN;
		if (g_ShipY > SHIP_Y_MAX)    g_ShipY = SHIP_Y_MAX;

		// ---- Rapid fire: spawn a bullet on cooldown if a slot is free
		if (g_FireTimer) g_FireTimer--;
		if (fire && g_FireTimer == 0)
		{
			for (i = 0; i < MAX_BULLETS; ++i)
				if (!g_BulletLive[i])
				{
					g_BulletLive[i] = TRUE;
					g_BulletX[i] = g_ShipX;
					g_BulletY[i] = g_ShipY - 12;
					g_FireTimer = FIRE_COOLDOWN;
					g_SfxShot = SHOT_LEN;
					break;
				}
		}

		// ---- Bullets travel up
		for (i = 0; i < MAX_BULLETS; ++i)
			if (g_BulletLive[i])
			{
				g_BulletY[i] -= BULLET_SPEED;
				if (g_BulletY[i] < 8)
					g_BulletLive[i] = FALSE;
			}

		// ---- Enemy stream: scroll horizontally (wrap), weave by sine, drift down
		if (++g_DescentTimer >= 90)
		{
			g_DescentTimer = 0;
			if (g_BaseY < SHIP_Y_MIN - 8) g_BaseY++;
		}
		for (i = 0; i < ENEMY_COUNT; ++i)
		{
			if (!g_EnemyLive[i]) continue;
			g_EnemyX[i] += g_WaveDir * g_WaveSpeed;
			if (g_EnemyX[i] < 0)         g_EnemyX[i] += SCREEN_W;
			if (g_EnemyX[i] >= SCREEN_W) g_EnemyX[i] -= SCREEN_W;
			g_EnemyY[i] = EnemyYFor(g_EnemyX[i]);
		}

		// ---- Collisions
		for (i = 0; i < ENEMY_COUNT; ++i)
		{
			if (!g_EnemyLive[i]) continue;

			// bullet hits enemy
			for (j = 0; j < MAX_BULLETS; ++j)
			{
				if (!g_BulletLive[j]) continue;
				if (Hit16(g_BulletX[j], g_BulletY[j], g_EnemyX[i], g_EnemyY[i]))
				{
					g_BulletLive[j] = FALSE;
					g_EnemyLive[i]  = FALSE;
					g_SfxBoom = BOOM_LEN;
					g_Score += 20;
					g_WaveLeft--;
					// keep the stream full until the quota nears zero
					{
						u8 live = 0, k;
						for (k = 0; k < ENEMY_COUNT; ++k) if (g_EnemyLive[k]) live++;
						u8 need = (g_WaveLeft < ENEMY_COUNT) ? g_WaveLeft : ENEMY_COUNT;
						if (live < need)
						{
							SpawnEnemy(i);
							g_EnemyY[i] = EnemyYFor(g_EnemyX[i]);
						}
					}
					DrawHud();
					break;
				}
			}

			// enemy reaches the ship
			if (g_EnemyLive[i] && Hit16(g_ShipX, g_ShipY, g_EnemyX[i], g_EnemyY[i]))
				died = TRUE;
		}

		// ---- Energy drains continuously; empty == lost ship
		if (++g_DrainTimer >= g_DrainRate)
		{
			g_DrainTimer = 0;
			g_Energy--;
			DrawEnergyBar(FALSE);
			if (g_Energy <= 0)
				died = TRUE;
		}

		// ---- Wave cleared?
		if (g_WaveLeft == 0)
		{
			for (i = 0; i < ENEMY_COUNT; ++i)
				VDP_SetSpritePositionY(SPR_ENEMY_FIRST + i, SPR_OFF_Y);
			VDP_SetSpritePositionY(SPR_SHIP, SPR_OFF_Y);
			WaveCleared();
			g_Level++;
			LoadWave();
			ResetPlayer();
			DrawHud();
			frame = 0;
			continue;
		}

		// ---- Lost a ship?
		if (died)
		{
			g_SfxBoom = BOOM_LEN;
			g_Lives--;
			DrawHud();
			Pause(45);
			if (g_Lives == 0)
				return;					// -> game over
			g_Energy = ENERGY_MAX;
			g_EnergyDrawn = -1;
			DrawEnergyBar(TRUE);
			for (i = 0; i < ENEMY_COUNT; ++i)	// re-spread the stream away from the ship
				if (g_EnemyLive[i])
				{
					g_EnemyX[i] = (i * (SCREEN_W / ENEMY_COUNT));
					g_EnemyY[i] = EnemyYFor(g_EnemyX[i]);
				}
			ResetPlayer();
		}

		// ---- Render
		VDP_SetSprite(SPR_SHIP, g_ShipX, g_ShipY, SHAPE_SHIP);
		for (i = 0; i < MAX_BULLETS; ++i)
		{
			if (g_BulletLive[i])
				VDP_SetSprite(SPR_BULLET_FIRST + i, g_BulletX[i], g_BulletY[i], SHAPE_BULLET);
			else
				VDP_SetSpritePositionY(SPR_BULLET_FIRST + i, SPR_OFF_Y);
		}
		for (i = 0; i < ENEMY_COUNT; ++i)
		{
			if (g_EnemyLive[i])
				VDP_SetSprite(SPR_ENEMY_FIRST + i, g_EnemyX[i], g_EnemyY[i], enemyShape);
			else
				VDP_SetSpritePositionY(SPR_ENEMY_FIRST + i, SPR_OFF_Y);
		}

		SoundUpdate();
	}
}

//=============================================================================
// MAIN
//=============================================================================

void main()
{
	VDP_SetMode(VDP_MODE_SCREEN5);
	VDP_SetColor(COLOR_BLACK);
	VDP_EnableVBlank(TRUE);
	VDP_ClearVRAM();

	VDP_SetSpriteFlag(VDP_SPRITE_SIZE_16 + VDP_SPRITE_SCALE_1);
	Print_SetBitmapFont(g_Font_MGL_Sample6);
	Print_SetColor(COLOR_WHITE, COLOR_BLACK);

	SoundInit();

	while (TRUE)
	{
		TitleScreen();
		PlayGame();
		GameOverScreen();
	}
}
