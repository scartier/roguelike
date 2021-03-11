// 7DRL-like
// 2021-Feb-23

#define DC 0  // don't care
#define NA 0  // not applicable
#define null 0

byte faceOffsetArray[] = { 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5 };

#define CCW_FROM_FACE(f, amt) faceOffsetArray[6 + (f) - (amt)]
#define CW_FROM_FACE(f, amt)  faceOffsetArray[(f) + (amt)]
#define OPPOSITE_FACE(f)      CW_FROM_FACE((f), 3)

// ----------------------------------------------------------------------------------------------------

enum GameState
{
  GameState_Init,
  GameState_Descend,
  GameState_Play,
  GameState_Ascend,
  GameState_Lose,
  GameState_Win
};

GameState gameState = GameState_Init;
byte levelNum = 0;

enum TileRole
{
  TileRole_Init,
  TileRole_Player,
  TileRole_Adjacent,
  TileRole_None
};

TileRole tileRole = TileRole_Init;

enum Direction
{
  Direction_CW,
  Direction_CCW
};

// ----------------------------------------------------------------------------------------------------
// ROOM CONFIG
// Defines how a room looks. It can be in one of three states: solid wall, totally empty, or half
// empty for a corridor. That corridor can be rotated one of six directions.

enum RoomConfig
{
  RoomConfig_Corridor0,   // corridor faces 012 relative to Map North
  RoomConfig_Corridor1,   // corridor faces 123 relative to Map North
  RoomConfig_Corridor2,   // corridor faces 234 relative to Map North
  RoomConfig_Corridor3,   // corridor faces 345 relative to Map North
  RoomConfig_Corridor4,   // corridor faces 450 relative to Map North
  RoomConfig_Corridor5,   // corridor faces 501 relative to Map North
  RoomConfig_Open,
  RoomConfig_Solid
};

#define IS_CORRIDOR(x) ((x) <= RoomConfig_Corridor5)

// ----------------------------------------------------------------------------------------------------
// ROOM TEMPLATE
// Used during dungeon generation
// Describes how rooms are connected together

struct RoomTemplate
{
  byte roomConfig : 3;
  byte unused : 5;              // padding for byte boundary
  byte exitTemplates[3][2];     // three exit faces with choice of two templates each
};

RoomTemplate roomTemplates[] =
{
  { RoomConfig_Open,      DC, { {  1,  1 }, { NA, NA }, {  2,  2 } } },
  { RoomConfig_Corridor4, DC, { {  3,  3 }, { NA, NA }, { NA, NA } } },
  { RoomConfig_Corridor0, DC, { { NA, NA }, { NA, NA }, {  3,  3 } } },
  { RoomConfig_Open,      DC, { { NA, NA }, { NA, NA }, { NA, NA } } },
};

// ----------------------------------------------------------------------------------------------------
// ROOM DATA
// Persisted room data while the player is on the level

struct RoomCoord
{
  byte x : 4;
  byte y : 4;
};

struct RoomDataLevelGen
{
  RoomCoord coord;

  byte roomConfig : 3;
  byte templateIndex : 4;
  byte UNUSED     : 1;

  byte enemyType  : 3;
  byte entryFace  : 3;
  byte enemyInfo  : 2;    // enemy-dependent state information

  byte itemType   : 3;
  byte UNUSED2    : 3;
  byte itemInfo   : 2;    // item-dependent state information
};

struct RoomDataGameplay
{
  RoomCoord coord;

  byte roomConfig : 3;
  byte UNUSED     : 4;
  byte RESERVED   : 1;    // 'toggle' bit when communicating

  byte enemyType  : 3;
  byte enemyFace  : 3;    // face location for the enemy
  byte enemyInfo  : 2;    // enemy-dependent state information

  byte itemType   : 3;
  byte itemFace   : 3;    // face location for the item
  byte itemInfo   : 2;    // item-dependent state information
};

struct RoomDataFaceValue
{
  byte tileRole   : 2;    // this tile's role - for dynamic tile swapping
  byte fromFace   : 3;    // tells adjacent tile what face of the player tile it's attached to
  byte UNUSED1    : 3;

  byte roomConfig : 3;
  byte UNUSED2    : 4;
  byte toggle     : 1;    // toggles every time the data changes

  byte enemyType  : 3;
  byte enemyFace  : 3;
  byte enemyInfo  : 2;

  byte itemType   : 3;
  byte itemFace   : 3;
  byte itemInfo   : 2;
};

union RoomData
{
  RoomDataLevelGen  levelGen;
  RoomDataGameplay  gameplay;
  RoomDataFaceValue faceValue;
  uint32_t          rawBits;
};

#define MAX_ROOMS 10
RoomData levelRoomData[MAX_ROOMS];
byte maxRoomData = 0;

RoomData *currentRoom = null;

// ----------------------------------------------------------------------------------------------------
// GAME STATE

Direction playerDir = Direction_CW;
byte playerFace = 0;

#define PLAYER_MOVE_RATE (1000 >> 6)
byte playerMoveRate = PLAYER_MOVE_RATE;
Timer playerMoveTimer;

// State for adjacent tiles
byte relativeRotation;    // rotation relative to the player tile
bool tryToMoveHere = false;

// ----------------------------------------------------------------------------------------------------
// RENDER INFO

#define DIM_COLORS 0
#define RGB_TO_U16_WITH_DIM(r,g,b) ((((uint16_t)(r)>>3>>DIM_COLORS) & 0x1F)<<1 | (((uint16_t)(g)>>3>>DIM_COLORS) & 0x1F)<<6 | (((uint16_t)(b)>>3>>DIM_COLORS) & 0x1F)<<11)

#define COLOR_PLAYER      RGB_TO_U16_WITH_DIM( 192, 192, 192 )
#define COLOR_WALL        RGB_TO_U16_WITH_DIM( 192,  96,   0 )
#define COLOR_EMPTY       RGB_TO_U16_WITH_DIM(  48,  96,   0 )

/*
uint16_t renderLUT[] =
{
  RGB_TO_U16_WITH_DIM( 255,   0,    0 ),
  RGB_TO_U16_WITH_DIM(   0, 255,    0 ),
  RGB_TO_U16_WITH_DIM( 255,   0,  255 )
};

byte colorState[3];           // the current state displayed
byte overlayState[3];         // used for animations or to hide things
*/

// ====================================================================================================

void setup()
{
}

// ----------------------------------------------------------------------------------------------------

void loop()
{
  readFaceValues();

  switch (gameState)
  {
    case GameState_Init:    loopInit();    break;
    case GameState_Descend: loopDescend(); break;
    case GameState_Play:    loopPlay();    break;
  }

  updateFaceValues();

  render();

  // Consume click if not already done
  buttonSingleClicked();
}

// ----------------------------------------------------------------------------------------------------
// INIT

void loopInit()
{
  if (tileRole == TileRole_Init)
  {
    if (buttonSingleClicked() && !hasWoken())
    {
      tileRole = TileRole_Player;
      descendLevel();
    }
  }
}

// ----------------------------------------------------------------------------------------------------
// DESCEND

void descendLevel()
{
  gameState = GameState_Descend;

  levelNum++;
  generateLevel();

  playerFace = 0;
}

void loopDescend()
{
  startPlay();
}

// ----------------------------------------------------------------------------------------------------
// PLAY

void startPlay()
{
  gameState = GameState_Play;
  playerMoveTimer.set(playerMoveRate << 6);
}

void loopPlay()
{
  if (playerMoveTimer.isExpired())
  {
    playerFace = (playerDir == Direction_CW) ? CW_FROM_FACE(playerFace, 1) : CCW_FROM_FACE(playerFace, 1);
    playerMoveTimer.set(playerMoveRate << 6);
  }

  if (tileRole == TileRole_Adjacent)
  {
    // Clicking an adjacent tile will attempt to move the player there
    if (buttonSingleClicked() && !hasWoken())
    {
      if (currentRoom != null)
      {
        if (currentRoom->gameplay.roomConfig == RoomConfig_Open)
        {
          tryToMoveHere = true;
        }
        else if (currentRoom->gameplay.roomConfig != RoomConfig_Solid)
        {
          
        }
      }
    }
  }
}

// ====================================================================================================
// COMMUNICATION
// ====================================================================================================

// Read our neighbor face values
// Use that to update our role, if necessary
// For adjacent rooms, update the room info based on the value sent by the player tile
void readFaceValues()
{
  if (tileRole == TileRole_Player)
  {
    // Player tile doesn't care what the other tiles have to say
    return;
  }

  //
  // If not the player tile then infer our role based on our neighbors
  // Non-player tiles re-evaluate their role every frame to account for attach/detach
  //

  tileRole = TileRole_Init;
  currentRoom = null;
  FOREACH_FACE(f)
  {
    if (!isValueReceivedOnFaceExpired(f))
    {
      RoomData val;
      val.rawBits = getLastValueReceivedOnFace(f);
      if (val.faceValue.tileRole == TileRole_Player)
      {
        tileRole = TileRole_Adjacent;

        // Compute how much this tile is rotated relative to the player tile
        byte entryFace = OPPOSITE_FACE(val.faceValue.fromFace);
        relativeRotation = (f >= entryFace) ? (f - entryFace) : (6 + f - entryFace);

        levelRoomData[0] = val;
        currentRoom = &levelRoomData[0];    // non-player tiles use level data [0] to hold room info
        break;
      }
      else if (val.faceValue.tileRole != TileRole_Init)
      {
        tileRole = TileRole_None;
      }
    }
  }
}

void updateFaceValues()
{
  RoomData roomDataOut;

  FOREACH_FACE(f)
  {
    if (tileRole == TileRole_Player)
    {
      // If we're the player then transmit the adjacent room info
      if (currentRoom != null)
      {
        if (!isValueReceivedOnFaceExpired(f))
        {
          RoomCoord newCoord = nextCoord(currentRoom->gameplay.coord, f);
          RoomData *nextRoom = findRoom(newCoord);
          if (nextRoom == null)
          {
            // Non-existent room - output a filled space
            roomDataOut.faceValue.roomConfig = RoomConfig_Solid;
          }
          else
          {
            // Room exists - output its info
            roomDataOut.faceValue = nextRoom->faceValue;
          }
        }
      }

      roomDataOut.faceValue.fromFace = f;
    }

    // Set the tile role last because it shares bits with the map coordinates and must overwrite them
    roomDataOut.faceValue.tileRole = tileRole;
    
    setValueSentOnFace(roomDataOut.rawBits, f);
  }
}

// ====================================================================================================


// ====================================================================================================
// LEVEL GENERATION
// ====================================================================================================

// Seed with a starting room. Consider this the first pass. (for reference: ROOM = TILE)
// Do successive passes through the rooms, processing the rooms added in the previous pass.
// Each pass looks for rooms with exits and adds new rooms for the next pass.
// Once the level array is full, change all rooms in the final full pass to be open so that paths don't 
// end in a corridor.
// Post process all rooms to add items and enemies.
void generateLevel()
{
  maxRoomData = 0;

  // Seed with a starting room

  byte prevFullPassStartIndex = 0;
  generateRoom({ 7, 1 }, 3, 0);  // {X, Y}, entry face, template index
/*
  while (maxRoomData < MAX_ROOMS)
  {
*/
    // Loop through all the rooms added in the previous pass
    byte thisPassStartIndex = maxRoomData;

    for (byte roomIndex = prevFullPassStartIndex; roomIndex < thisPassStartIndex; roomIndex++)
    {
      RoomData *prevRoomData = &levelRoomData[roomIndex];
      RoomTemplate *roomTemplate = &roomTemplates[prevRoomData->levelGen.templateIndex];
      byte entryFace = prevRoomData->levelGen.entryFace;

      for (byte exitIndex = 0; exitIndex <= 2; exitIndex++)
      {
        if (roomTemplate->exitTemplates[exitIndex][0] != NA)
        {
          // TODO : Randomly choose a template
          byte newTemplateIndex = roomTemplate->exitTemplates[exitIndex][0];
          
          byte exitFace = CW_FROM_FACE(entryFace, 2 + exitIndex);
          RoomCoord newCoord = nextCoord(prevRoomData->levelGen.coord, exitFace);
          byte newEntryFace = OPPOSITE_FACE(exitFace);
          if (!generateRoom(newCoord, newEntryFace, newTemplateIndex))
          {
            // Room was not generated - must have filled the room data array
          }
        }
      }
    }
/*
  }
*/

  currentRoom = &levelRoomData[0];
}

bool generateRoom(RoomCoord coord, byte entryFace, byte templateIndex)
{
  RoomData *roomData = findRoom(coord);
  if (roomData != null)
  {
    // Tried to generate a room on top of an existing room
    // Force it to become fully open so it can accommodate all entrances
    roomData->levelGen.roomConfig = RoomConfig_Open;

    // TODO - superset of enemies & items
    return true;
  }

  // Got here so room does not already exist - try to create it

  // Check if room data array is full
  if (maxRoomData >= MAX_ROOMS)
  {
    return false;
  }

  // There's space for this room - allocate it
  roomData = &levelRoomData[maxRoomData];
  maxRoomData++;

  roomData->levelGen.coord = coord;

  // Retain the entry face for the next pass
  // It informs which faces can contain exits
  roomData->levelGen.entryFace = entryFace;

  // Copy the template info
  roomData->levelGen.templateIndex = templateIndex;
  RoomTemplate *roomTemplate = &roomTemplates[templateIndex];
  roomData->levelGen.roomConfig = roomTemplate->roomConfig;

  // TODO : Add enemies & items

  // Rotate corridors relative to where we came in
  if (IS_CORRIDOR(roomTemplate->roomConfig))
  {
    roomData->levelGen.roomConfig = CW_FROM_FACE(roomData->levelGen.roomConfig, entryFace);
  }

  return true;
}

// Looks for a room at the given coordinates and returns it if found, otherwise returns null
RoomData* findRoom(RoomCoord coord)
{
  for (byte i = 0; i < maxRoomData; i++)
  {
    if (levelRoomData[i].levelGen.coord.x == coord.x && levelRoomData[i].levelGen.coord.y == coord.y)
    {
      return &levelRoomData[i];
    }
  }

  return null;
}

// From a given coordinate, exiting by the given direction, returns the next coordinate
RoomCoord nextCoord(RoomCoord coord, byte exitFace)
{
  RoomCoord nextCoord = coord;

  switch (exitFace)
  {
    case 1:
    case 2:
      nextCoord.x = coord.x + 1;
      break;

    case 4:
    case 5:
      nextCoord.x = coord.x - 1;
      break;
  }

  switch (exitFace)
  {
    case 0:
      nextCoord.y = coord.y + 1;
      break;

    case 3:
      nextCoord.y = coord.y - 1;
      break;

    case 1:
    case 5:
      nextCoord.y = (coord.x & 1) ? coord.y + 1 : coord.y;
      break;

    case 2:
    case 4:
      nextCoord.y = (coord.x & 1) ? coord.y : coord.y - 1;
      break;
  }

  return nextCoord;
}

// ====================================================================================================

void renderRoom(RoomData *roomData)
{
  Color color;

  // Assume solid
  byte startFace = 0;
  byte emptyFaces = 6;

  switch (roomData->gameplay.roomConfig)
  {
    case RoomConfig_Solid:
      // All walls
      emptyFaces = 0;
      break;
      
    case RoomConfig_Open:
      break;
      
    default:
      // Corridor - half solid, half open
      startFace = roomData->gameplay.roomConfig;
      emptyFaces = 3;
      break;
  }

  // Factor in our rotation relative to the player tile
  if (tileRole == TileRole_Adjacent)
  {
    startFace = CW_FROM_FACE(startFace, relativeRotation);
  }

  // Draw the room walls and empty spaces
  FOREACH_FACE(f)
  {
    byte face = CW_FROM_FACE(startFace, f);
    color.as_uint16 = (f < emptyFaces) ? COLOR_EMPTY : COLOR_WALL;
    setColorOnFace(color, face);
  }

  // Draw the player above everything else
  if (tileRole == TileRole_Player)
  {
    color.as_uint16 = COLOR_PLAYER;
    setColorOnFace(color, playerFace);
  }
}

void renderFace(byte f)
{
  /*
    FaceStateGame *faceStateGame = &faceStatesGame[f];

    byte r = getColorFromState(colorState[0], f);
    byte g = getColorFromState(colorState[1], f);
    byte b = getColorFromState(colorState[2], f);

    byte overlayR = getColorFromState(overlayState[0], f);
    byte overlayG = getColorFromState(overlayState[1], f);
    byte overlayB = getColorFromState(overlayState[2], f);
    bool overlayNonZero = overlayR | overlayG | overlayB;
    bool hasOverlay = (overlayState[0] | overlayState[1] | overlayState[2]) & (1<<f);

    byte colorRGB[3];
    byte paused = false;
    bool startNextCommand = faceStateGame->animTimer.isExpired();
    uint32_t animRate32 = (uint32_t) faceStateGame->animRateDiv4 << 2;
    uint32_t t = (128 * (animRate32 - faceStateGame->animTimer.getRemaining())) / animRate32;  // 128 = 1.0

    AnimCommand animCommand = animSequences[faceStateGame->animIndexCur];
    switch (animCommand)
    {
    case AnimCommand_SolidBase:
      colorRGB[0] = r;
      colorRGB[1] = g;
      colorRGB[2] = b;
      startNextCommand = true;
      break;

    case AnimCommand_SolidOverlay:
      colorRGB[0] = overlayR;
      colorRGB[1] = overlayG;
      colorRGB[2] = overlayB;
      startNextCommand = true;
      break;

    case AnimCommand_SolidWithOverlay:
      colorRGB[0] = hasOverlay ? overlayR : r;
      colorRGB[1] = hasOverlay ? overlayG : g;
      colorRGB[2] = hasOverlay ? overlayB : b;
      startNextCommand = true;
      break;

    case AnimCommand_LerpOverlayIfNonZeroToBase:
    case AnimCommand_LerpOverlayToBase:
      t = 128 - t;
    case AnimCommand_LerpBaseToOverlayIfNonZero:
    case AnimCommand_LerpBaseToOverlay:
      colorRGB[0] = lerpColor(r, overlayR, t);
      colorRGB[1] = lerpColor(g, overlayG, t);
      colorRGB[2] = lerpColor(b, overlayB, t);

      if (animCommand == AnimCommand_LerpOverlayIfNonZeroToBase ||
          animCommand == AnimCommand_LerpBaseToOverlayIfNonZero)
      {
        if (!overlayNonZero)
        {
          colorRGB[0] = r;
          colorRGB[1] = g;
          colorRGB[2] = b;
        }
      }
      break;

    case AnimCommand_Pause:
    case AnimCommand_PauseHalf:
      paused = true;
      break;

    case AnimCommand_FadeInBase:
      t = 128 - t;
    case AnimCommand_FadeOutBase:
      // Force the code below to do the lerp
      overlayR = 1;
    case AnimCommand_FadeOutBaseIfOverlayR:
      colorRGB[0] = colorRGB[1] = colorRGB[2] = 0;
      if (overlayR)
      {
        colorRGB[0] = lerpColor(r, 0, t);
        colorRGB[1] = lerpColor(g, 0, t);
        colorRGB[2] = lerpColor(b, 0, t);
      }
      break;

    case AnimCommand_RandomRotateBaseAndOverlayR:
      colorState[0] = randGetByte();
      colorState[1] = randGetByte();
      colorState[2] = randGetByte();
      overlayState[0] = 1 << randRange(0, 6);

      paused = true;
      startNextCommand = true;
      break;

    case AnimCommand_RandomToolOnBase:
      byte randByte = randGetByte() | 0x2;
      byte toolPattern = (randByte & 0x3E) >> (randByte & 0x1) ;
      colorState[0] = colorState[1] = colorState[2] = toolPattern;

      paused = true;
      startNextCommand = true;
      break;
    }

    Color color = makeColorRGB(colorRGB[0], colorRGB[1], colorRGB[2]);
    if (paused)
    {
    color.as_uint16 = faceStateGame->savedColor;
    }
    faceStateGame->savedColor = color.as_uint16;
    setColorOnFace(color, f);

    if (startNextCommand)
    {
    faceStateGame->animIndexCur++;

    // If we finished the sequence, loop back to the beginning
    if (animSequences[faceStateGame->animIndexCur] == AnimCommand_Loop)
    {
      faceStateGame->animIndexCur = faceStateGame->animIndexStart;
    }
    else if (animSequences[faceStateGame->animIndexCur] == AnimCommand_Done)
    {
      faceStateGame->animIndexCur--;
      faceStateGame->animDone = true;
    }

    // Start timer for next command
    if (animSequences[faceStateGame->animIndexCur] == AnimCommand_PauseHalf)
    {
      animRate32 >>= 1;
    }
    faceStateGame->animTimer.set(animRate32);
    }
  */
}

void render()
{
  setColor(OFF);

  if (currentRoom != null)
  {
    renderRoom(currentRoom);
  }
  
/*
  switch (tileRole)
  {
    case TileRole_Init:
      setColor(WHITE);
      break;
    case TileRole_Player:
      setColor(RED);
      break;
    case TileRole_Adjacent:
      setColor(BLUE);
      break;
    case TileRole_None:
      setColor(OFF);
      break;
  }
*/
}

// CHANGELIST
//
