// 7DRL-like
// 2021-Feb-23

#define DC 0  // don't care
#define NA 0  // not applicable
#define null 0
#define INVALID_FACE 7

#define MAX(n1,n2)            ((n1) > (n2) ? (n1) : (n2))

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

byte randState;

// ----------------------------------------------------------------------------------------------------
// ROOM CONFIG
// Defines how a room looks. It can either be solid wall or empty.

enum RoomConfig
{
  RoomConfig_Corridor0,
  RoomConfig_Corridor1,
  RoomConfig_Corridor2,
  RoomConfig_Corridor3,
  RoomConfig_Corridor4,
  RoomConfig_Corridor5,
  RoomConfig_Open,
  RoomConfig_Solid
};

// ----------------------------------------------------------------------------------------------------
// MONSTERS

enum MonsterClass
{
  MonsterClass_None,
  MonsterClass_EASY,
  MonsterClass_MEDIUM,
  MonsterClass_HARD
};

enum MonsterType
{
  MonsterType_None,
  MonsterType_Rat           // moves around the tile, switching directions occasionally
};

//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\
// RAT
// Moves around the room slowly, randomly biting
enum MonsterState_Rat
{
  MonsterState_Rat_Walk,
  MonsterState_Rat_Pause,
  MonsterState_Rat_Bite
};
#define RAT_WALK_RATE   1500
#define RAT_PAUSE_RATE  2000
#define RAT_BITE_RATE   1500
//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\//\\

// Timers for moving monsters in the player tile and adjacent tiles
Timer monsterMoveTimer;
Timer adjacentMonsterMoveTimers[FACE_COUNT];

// ----------------------------------------------------------------------------------------------------
// ROOM TEMPLATE
// Used during dungeon generation
// Describes how rooms are connected together

struct RoomTemplate
{
  byte roomConfig : 3;
  MonsterClass monsterClass : 2;    // difficulty of possible monster present (0=none)
  byte unused : 3;              // padding for byte boundary
  byte exitTemplates[3][2];     // three exit faces with choice of two templates each
};

RoomTemplate roomTemplates[] =
{
  { RoomConfig_Open, MonsterClass_None, DC, { {  1,  1 }, { NA, NA }, {  1,  1 } } },

  { RoomConfig_Open, MonsterClass_EASY, DC, { { NA, NA }, { NA, NA }, { NA, NA } } },

  // Straight empty corridor
  { RoomConfig_Open, MonsterClass_None, DC, { { NA, NA }, {  2,  2 }, { NA, NA } } },

  // Four-chamber room with two exits
  { RoomConfig_Open, MonsterClass_None, DC, { {  3,  3 }, {  4,  4 }, {  3,  3 } } },
  { RoomConfig_Open, MonsterClass_EASY, DC, { { NA, NA }, {  5,  5 }, { NA, NA } } },
  { RoomConfig_Open, MonsterClass_None, DC, { { NA, NA }, { NA, NA }, { NA, NA } } },

  // Dead end
  { RoomConfig_Open, MonsterClass_None, DC, { { NA, NA }, { NA, NA }, { NA, NA } } },
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

  MonsterClass monsterClass  : 3;
  byte entryFace  : 3;
  byte monsterState : 2;    // monster-dependent state information

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

  MonsterType monsterType  : 3;
  byte monsterFace  : 3;    // face location for the monster
  byte monsterState : 2;    // monster-dependent state information

  byte itemType   : 3;
  byte itemFace   : 3;    // face location for the item
  byte itemInfo   : 2;    // item-dependent state information
};

struct RoomDataFaceValue
{
  byte tileRole   : 2;    // this tile's role - for dynamic tile swapping
  byte fromFace   : 3;    // PLAYER -> ADJACENT: tells adjacent tile what face of the player tile it's attached to
  byte moveHere   : 1;    // ADJACENT -> PLAYER: flag to tell player to try to move here
  byte showPlayer : 1;    // PLAYER -> ADJACENT: tells adjacent tile to show the player on its tile before transitioning
  byte UNUSED1    : 1;

  byte roomConfig : 3;
  byte gameState  : 3;    // PLAYER -> ADJACENT
  byte UNUSED2    : 1;
  byte toggle     : 1;    // toggles every time the data changes

  MonsterType monsterType  : 3;
  byte monsterFace  : 3;
  byte monsterState : 2;

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

#define MAX_ROOMS 20
RoomData levelRoomData[MAX_ROOMS + 1];
byte maxRoomData = 0;

RoomData *currentRoom = null;

// ----------------------------------------------------------------------------------------------------
// GAME STATE

// PLAYER TILE state
Direction playerDir = Direction_CW;
byte playerFace = 0;
byte playerHitPoints = 3;
#define BACKGROUND_PULSE_RATE 2000
Timer backgroundPulseTimer;

enum PulseReason
{
  PulseReason_PlayerDamaged,
  PulseReason_MonsterDamaged
};
PulseReason pulseReason;

#define PLAYER_MOVE_RATE (1000 >> 6)
byte playerMoveRate = PLAYER_MOVE_RATE;
Timer playerMoveTimer;
bool canPausePlayerMovement = true;
bool pausePlayerMovement = false;

byte moveToFace = INVALID_FACE;
#define MOVE_DELAY 700
Timer moveDelayTimer;
bool movingToNewRoom = false;

byte toggleMask = 0;

// ADJACENT TILE state
byte entryFace;             // virtual face the player will enter
byte relativeRotation = 0;  // rotation relative to the player tile
bool tryToMoveHere = false;

// ----------------------------------------------------------------------------------------------------
// RENDER INFO

#define DIM_COLORS 0
#define RGB_TO_U16_WITH_DIM(r,g,b) ((((uint16_t)(r)>>DIM_COLORS) & 0x1F)<<1 | (((uint16_t)(g)>>DIM_COLORS) & 0x1F)<<6 | (((uint16_t)(b)>>DIM_COLORS) & 0x1F)<<11)

#define COLOR_PLAYER      RGB_TO_U16_WITH_DIM( 24, 24, 24 )
#define COLOR_WALL        RGB_TO_U16_WITH_DIM( 16,  8,  0 )
#define COLOR_EMPTY       RGB_TO_U16_WITH_DIM(  6, 12,  0 )

#define COLOR_DANGER      RGB_TO_U16_WITH_DIM( 24,  3,  0 )
#define COLOR_MONSTER_RAT RGB_TO_U16_WITH_DIM(  8, 24, 20 )

// ====================================================================================================

byte __attribute__((noinline)) randGetByte()
{
  // https://doitwireless.com/2014/06/26/8-bit-pseudo-random-number-generator/
  byte next = randState >> 1;
  if (randState & 1)
  {
    next ^= 0xB8;
  }
  randState = next;
  return next;
}

byte __attribute__((noinline)) randRange(byte min, byte max)
{
  uint32_t val = randGetByte();
  uint32_t range = max - min;
  val = (val * range) >> 8;
  return val + min;
}

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
      // Button clicking provides our entropy for the initial random seed
      randState = millis();
      
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
  if (tileRole == TileRole_Player)
  {
    loopPlay_Player();
  }
  else if (tileRole == TileRole_Adjacent)
  {
    loopPlay_Adjacent();
  }
}

void loopPlay_Player()
{
  // If in the middle of moving tiles then don't do anything else
  if (movingToNewRoom && moveDelayTimer.isExpired())
  {
    // Delay again once we shift displays so the player has some time to get adjusted
    moveDelayTimer.set(MOVE_DELAY);
    movingToNewRoom = false;
    
    // Get the room we are moving to
    RoomCoord nextRoomCoord = nextCoord(currentRoom->gameplay.coord, playerFace);
    currentRoom = findRoom(nextRoomCoord);

    playerFace = OPPOSITE_FACE(playerFace);
    playerMoveTimer.set(playerMoveRate << 6);

    // Force all tiles to update
    toggleMask = ~toggleMask;
  
    return;
  }

  if (!moveDelayTimer.isExpired())
  {
    return;
  }
  
  // Holding the button down pauses movement for the player
  // Long pressing switches directions
  if (buttonDown())
  {
    if (canPausePlayerMovement)
    {
      pausePlayerMovement = true;
      if (buttonLongLongPressed())
      {
        canPausePlayerMovement = false;
        pausePlayerMovement = false;
        playerDir = (playerDir == Direction_CW) ? Direction_CCW : Direction_CW;
      }
    }
  }
  else
  {
    pausePlayerMovement = false;
    canPausePlayerMovement = true;
  }
  // Clear bits when not used
  buttonLongLongPressed();

  // Player automatically moves around the current room
  if (playerMoveTimer.isExpired() && !pausePlayerMovement)
  {
    playerFace = (playerDir == Direction_CW) ? CW_FROM_FACE(playerFace, 1) : CCW_FROM_FACE(playerFace, 1);
    playerMoveTimer.set(playerMoveRate << 6);
    checkCollisions();
  }

  // Move the player to the next room
  if (currentRoom != null)
  {
    if (!movingToNewRoom && playerFace == moveToFace)
    {
      movingToNewRoom = true;
      moveDelayTimer.set(MOVE_DELAY);

      moveToFace = INVALID_FACE;

      // Force the other room to clear its move flag
      toggleMask ^= 1<<playerFace;
    }
  }

  // Move monsters in this room and all adjacent rooms
  if (currentRoom != null)
  {
    loopPlay_MoveMonster(currentRoom, &monsterMoveTimer);
    
    FOREACH_FACE(f)
    {
      RoomCoord newCoord = nextCoord(currentRoom->gameplay.coord, f);
      RoomData *nextRoom = findRoom(newCoord);
      if (nextRoom != null)
      {
        loopPlay_MoveMonster(nextRoom, &adjacentMonsterMoveTimers[f]);
      }
    }
  }

}

void loopPlay_MoveMonster(RoomData *roomData, Timer *moveTimer)
{
  switch (roomData->gameplay.monsterType)
  {
    case MonsterType_None:  return;
    case MonsterType_Rat:   moveRat(roomData, moveTimer);
  }

  if (tileRole == TileRole_Player)
  {
    checkCollisions();
  }
}

void moveRat(RoomData *roomData, Timer *moveTimer)
{
  if (!moveTimer->isExpired())
  {
    return;
  }
  
  switch ((MonsterState_Rat) roomData->gameplay.monsterState)
  {
    case MonsterState_Rat_Walk:
      // Use the room x-coordinate to select the rat's walk direction
      roomData->gameplay.monsterFace =
        (roomData->gameplay.coord.x & 0x1)
        ? CW_FROM_FACE(roomData->gameplay.monsterFace, 1)
        : CCW_FROM_FACE(roomData->gameplay.monsterFace, 1);

      if ((randGetByte() & 0x7) == 0x2)
      {
        roomData->gameplay.monsterState = MonsterState_Rat_Pause;
        moveTimer->set(RAT_PAUSE_RATE);
      }
      else
      {
        moveTimer->set(RAT_WALK_RATE);
      }
      break;
    
    case MonsterState_Rat_Pause:
      roomData->gameplay.monsterState = MonsterState_Rat_Bite;
      moveTimer->set(RAT_BITE_RATE);
      break;
    
    case MonsterState_Rat_Bite:
      roomData->gameplay.monsterState = MonsterState_Rat_Walk;
      moveTimer->set(RAT_WALK_RATE);
      break;
  }
}

void checkCollisions()
{
  if (currentRoom == null)
  {
    return;
  }

  if (!backgroundPulseTimer.isExpired())
  {
    // Player was already hit recently, don't let them get hit again
    return;
  }

//  byte monsterFace = currentRoom->gameplay.monsterFace;
//  monsterFace = CW_FROM_FACE(monsterFace, relativeRotation);
  
  switch (currentRoom->gameplay.monsterType)
  {
    case MonsterType_None:
      return;
      
    case MonsterType_Rat:
      if (playerFace == currentRoom->gameplay.monsterFace)
      {
        backgroundPulseTimer.set(BACKGROUND_PULSE_RATE);
        if ((MonsterState_Rat) currentRoom->gameplay.monsterState == MonsterState_Rat_Bite)
        {
          // Player takes damage!
          pulseReason = PulseReason_PlayerDamaged;
        }
        else
        {
          // Player damages monster!
          pulseReason = PulseReason_MonsterDamaged;
          // Rats only have 1 hp - remove it!
//          currentRoom->gameplay.monsterType = MonsterType_None;
        }
      }
      break;
  }
  
  if (!backgroundPulseTimer.isExpired())
  {
    if (playerHitPoints > 0)
    {
      // Player took a hit
      playerHitPoints--;
    }
  }
}

void loopPlay_Adjacent()
{
  // Clicking an adjacent tile will attempt to move the player there
  if (buttonSingleClicked() && !hasWoken())
  {
    if (currentRoom != null)
    {
      if (currentRoom->gameplay.roomConfig == RoomConfig_Open)
      {
        tryToMoveHere = !tryToMoveHere;
      }
      else if (currentRoom->gameplay.roomConfig != RoomConfig_Solid)
      {
        // Corridor - check if there's a wall blocking movement on this face
        byte corridorFace1 = currentRoom->gameplay.roomConfig;
        byte corridorFace2 = CW_FROM_FACE(corridorFace1, 1);
        byte corridorFace3 = CW_FROM_FACE(corridorFace1, 2);
        if (entryFace == corridorFace1 || entryFace == corridorFace2 || entryFace == corridorFace3)
        {
          tryToMoveHere = !tryToMoveHere;
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
    // Player tile mostly doesn't care what the other tiles have to say
    // Exception is movement
    byte oldMoveToFace = moveToFace;
    moveToFace = INVALID_FACE;
    FOREACH_FACE(f)
    {
      if (!isValueReceivedOnFaceExpired(f))
      {
        RoomData val;
        val.rawBits = getLastValueReceivedOnFace(f);

        // Ensure the toggle bits match before we consider its data
        if (val.faceValue.toggle != ((toggleMask >> f) & 0x1))
        {
          continue;
        }

        // Check if the player clicked a new room
        // Deselect the old by telling the tile to reset itself via the toggle bit
        if (val.faceValue.moveHere == 1)
        {
          moveToFace = f;
            
          if (oldMoveToFace == f || oldMoveToFace == INVALID_FACE)
          {
            // Same face we saw last time - assume it's the only one and skip
            continue;
          }

          // Got here so found a second face with the move flag set
          // Since it is different from the one last time, assume the player clicked it second and deselect the other(s)
          toggleMask ^= ~(1 << f);
          break;
        }
      }
    }
    
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
        entryFace = OPPOSITE_FACE(val.faceValue.fromFace);
        relativeRotation = (f >= entryFace) ? (f - entryFace) : (6 + f - entryFace);

        gameState = (GameState) val.faceValue.gameState;

        levelRoomData[0] = val;
        currentRoom = &levelRoomData[0];    // non-player tiles use level data [0] to hold room info

        if (val.faceValue.toggle != toggleMask)
        {
          resetTileState();
        }
        toggleMask = val.faceValue.toggle;
        break;
      }
      else if (val.faceValue.tileRole != TileRole_Init)
      {
        tileRole = TileRole_None;
      }
    }
  }
}

void resetTileState()
{
  tryToMoveHere = false;
}

void updateFaceValues()
{
  RoomData roomDataOut;

  FOREACH_FACE(f)
  {
    roomDataOut.faceValue.showPlayer = false;

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
            if (movingToNewRoom && f == playerFace)
            {
              roomDataOut.faceValue.showPlayer = true;
            }
          }
        }
      }

      roomDataOut.faceValue.fromFace = f;
      roomDataOut.faceValue.gameState = gameState;
      roomDataOut.faceValue.toggle = (toggleMask >> f) & 0x1;
    }
    else if (tileRole == TileRole_Adjacent)
    {
      roomDataOut.faceValue.moveHere = tryToMoveHere;
      roomDataOut.faceValue.toggle = toggleMask;
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
// Post process all rooms to add items and monsters.
void generateLevel()
{
  maxRoomData = 0;

  // Seed with a starting room

  byte prevFullPassStartIndex = 0;
  generateRoom({ 7, 1 }, 3, 0);  // {X, Y}, entry face, template index

  while (maxRoomData < MAX_ROOMS)
  {
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

          // Skip if the new room would be placed at the edge of the map
          RoomCoord newCoord = nextCoord(prevRoomData->levelGen.coord, exitFace);
          if (newCoord.x == 0 || newCoord.y == 0 || newCoord.x == 15 || newCoord.y == 15)
          {
            continue;
          }
          
          byte newEntryFace = OPPOSITE_FACE(exitFace);
          if (!generateRoom(newCoord, newEntryFace, newTemplateIndex))
          {
            // Room was not generated - must have filled the room data array
          }
        }
      }
    }

    // If we get through a pass without generating any new rooms then we're done
    if (thisPassStartIndex == maxRoomData)
    {
      break;
    }
  }

  // All rooms generated - post process to insert items and monsters
  for (byte roomIndex = 0; roomIndex < maxRoomData; roomIndex++)
  {
      RoomData *roomData = &levelRoomData[roomIndex];

      if (roomData->levelGen.monsterClass != MonsterClass_None)
      {
        // Overwrite the monster class with the actual monster type
        roomData->gameplay.monsterType = MonsterType_Rat;
        roomData->gameplay.monsterFace = 3;
        roomData->gameplay.monsterState = MonsterState_Rat_Walk;
      }
  }

  currentRoom = &levelRoomData[0];
}

bool generateRoom(RoomCoord coord, byte entryFace, byte templateIndex)
{
  RoomTemplate *roomTemplate = &roomTemplates[templateIndex];

  RoomData *roomData = findRoom(coord);
  if (roomData != null)
  {
    // Tried to generate a room on top of an existing room
    // Allow it, but take the most advanced enemy/item

    // Take the worst-case monster class >:O
    roomData->levelGen.monsterClass = (MonsterClass) MAX(roomData->levelGen.monsterClass, roomTemplate->monsterClass);

    // TODO - superset of items
    
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
  roomData->levelGen.roomConfig = roomTemplate->roomConfig;
  roomData->levelGen.monsterClass = roomTemplate->monsterClass;

  // TODO : Add items

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
    if (tileRole == TileRole_Player)
    {
      // If player got hit, flash the background red
      if (!backgroundPulseTimer.isExpired())
      {
        byte pulseIntensity = (backgroundPulseTimer.getRemaining() * 31) / BACKGROUND_PULSE_RATE;
        switch (pulseReason)
        {
          case PulseReason_PlayerDamaged:   color.r |= pulseIntensity; break;
          case PulseReason_MonsterDamaged:  color.r |= pulseIntensity; color.g |= pulseIntensity; break;
        }
      }
    }
    else if (tileRole == TileRole_Adjacent)
    {
      if (tryToMoveHere)
      {
        uint16_t a = (color.as_uint16 << 1) & 0b1111011110111100;
        color.as_uint16 |= a;//((color.as_uint16 >> 1) & 0b0111101111011110) + 0b01000100010000;
      }
    }
    setColorOnFace(color, face);
  }

  // Draw monsters
  if (roomData->gameplay.roomConfig == RoomConfig_Open)
  {
    if (roomData->gameplay.monsterType == MonsterType_Rat)
    {
      color.as_uint16 = roomData->gameplay.monsterState == MonsterState_Rat_Bite ? COLOR_DANGER : COLOR_MONSTER_RAT;
      byte face = roomData->gameplay.monsterFace;
      face = CW_FROM_FACE(face, relativeRotation);
      setColorOnFace(color, face);
    }
  }
  
/*
  // Highlight the tile that was clicked
  if (tileRole == TileRole_Adjacent)
  {
    if (tryToMoveHere)
    {
      setColor(RED);
    }
  }
  */
  // Draw the player above everything else
  color.as_uint16 = COLOR_PLAYER;
  if (tileRole == TileRole_Player)
  {
    if (!movingToNewRoom)
    {
      setColorOnFace(color, playerFace);
    }
  }
  else if (tileRole == TileRole_Adjacent && roomData->faceValue.showPlayer)
  {
    byte face = CW_FROM_FACE(entryFace, relativeRotation);
    setColorOnFace(color, face);
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
