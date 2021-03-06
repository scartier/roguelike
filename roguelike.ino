  Color color;
// 7DRL-like
// 2021-Feb-23

#define DEBUG_SHOW_KEYPATH 0

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
// ITEMS

enum ItemClass
{
  ItemClass_None,
  ItemClass_Any
};

enum ItemType
{
  ItemType_None,
  ItemType_Key,
  ItemType_LockedDoor,
  ItemType_OpenDoor,
  ItemType_Exit
};

// ----------------------------------------------------------------------------------------------------
// ROOM TEMPLATE
// Used during dungeon generation
// Describes how rooms are connected together

struct RoomTemplate
{
  MonsterClass monsterClass : 2;    // difficulty of possible monster present (0=none)
  ItemClass itemClass : 2;          // general type of items that can be here
  byte unused : 4;                  // padding for byte boundary
  byte exitTemplates[3][2];         // three exit faces with choice of two templates each
};

RoomTemplate roomTemplates[] =
{
  { MonsterClass_None, ItemClass_None, DC, { {  1,  1 }, { NA, NA }, {  1,  1 } } },

  // Straight empty corridor
  { MonsterClass_None, ItemClass_None, DC, { { NA, NA }, {  2,  2 }, { NA, NA } } },

  // Four-chamber room with two exits
  { MonsterClass_None, ItemClass_None, DC, { {  3,  3 }, {  4,  4 }, {  3,  3 } } },
  { MonsterClass_EASY, ItemClass_None, DC, { { NA, NA }, {  5,  5 }, { NA, NA } } },
  { MonsterClass_None, ItemClass_Any,  DC, { { NA, NA }, { NA, NA }, { NA, NA } } },

  // Dead end
  { MonsterClass_None, ItemClass_Any, DC, { { NA, NA }, { NA, NA }, { NA, NA } } },
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

  byte roomPresent: 1;
  byte UNUSED1    : 2;
  byte templateIndex : 4;
  byte keyPath    : 1;            // flag indicating the path where the key should be found

  MonsterClass monsterClass  : 3;
  byte entryFace  : 3;
  byte UNUSED2    : 2;

  byte UNUSED3    : 5;
  ItemClass itemClass  : 3;    // make sure this field doesn't clash with 'itemType'
};

struct RoomDataGameplay
{
  RoomCoord coord;

  byte roomPresent: 1;
  byte UNUSED2    : 5;
  byte TMP_KeyPath: 1;
  byte RESERVED   : 1;    // 'toggle' bit when communicating

  MonsterType monsterType  : 3;
  byte monsterFace  : 3;  // face location for the monster
  byte monsterState : 2;  // monster-dependent state information

  ItemType itemType   : 3;
  byte itemFace   : 3;    // face location for the item
  byte itemInfo   : 2;    // item-dependent state information
};

struct RoomDataFaceValue
{
  byte tileRole   : 2;    // this tile's role - for dynamic tile swapping
  byte fromFace   : 3;    // PLAYER -> ADJACENT: tells adjacent tile what face of the player tile it's attached to
  byte moveInfo   : 1;    // PLAYER -> ADJACENT: flag if movement possible, ADJACENT -> PLAYER: flag to tell player to try to move here
  byte showPlayer : 1;    // PLAYER -> ADJACENT: tells adjacent tile to show the player on its tile before transitioning
  byte UNUSED1    : 1;

  byte roomPresent: 1;
  byte UNUSED2    : 2;
  byte gameState  : 3;    // PLAYER -> ADJACENT
  byte TMP_KeyPath: 1;
  byte toggle     : 1;    // toggles every time the data changes

  MonsterType monsterType  : 3;
  byte monsterFace  : 3;
  byte monsterState : 2;

  ItemType itemType   : 3;
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
#define LOSE_GAME_FADE_RATE 50
byte playerMoveRate = PLAYER_MOVE_RATE;
Timer playerMoveTimer;
bool canPausePlayerMovement = true;
bool pausePlayerMovement = false;

byte moveToFace = INVALID_FACE;
#define MOVE_DELAY 700
Timer moveDelayTimer;
bool movingToNewRoom = false;

bool playerHasKey = false;

byte millisByte = 0;

byte toggleMask = 0;

// ADJACENT TILE state
byte entryFace;             // virtual face the player will enter
byte relativeRotation = 0;  // rotation relative to the player tile
bool tryToMoveHere = false;

// ----------------------------------------------------------------------------------------------------
// RENDER INFO

#define DIM_COLORS 0
#define RGB_TO_U16_WITH_DIM(r,g,b) ((((uint16_t)(r)>>DIM_COLORS) & 0x1F)<<1 | (((uint16_t)(g)>>DIM_COLORS) & 0x1F)<<6 | (((uint16_t)(b)>>DIM_COLORS) & 0x1F)<<11)

#define COLOR_PLAYER      RGB_TO_U16_WITH_DIM( 31, 31, 31 )
#define COLOR_WALL        RGB_TO_U16_WITH_DIM( 16,  8,  0 )
#define COLOR_EMPTY       RGB_TO_U16_WITH_DIM(  6, 15,  1 )
#define COLOR_KEYPATH     RGB_TO_U16_WITH_DIM(  0,  6,  9 )
#define COLOR_DOOR        RGB_TO_U16_WITH_DIM(  0, 18, 21 )
#define COLOR_EXIT        RGB_TO_U16_WITH_DIM(  6, 15,  1 )
#define COLOR_KEY1        RGB_TO_U16_WITH_DIM( 31,  0, 31 )
#define COLOR_KEY2        RGB_TO_U16_WITH_DIM( 24,  0, 24 )

#define COLOR_DANGER      RGB_TO_U16_WITH_DIM( 24,  3,  0 )
#define COLOR_MONSTER_RAT RGB_TO_U16_WITH_DIM( 24, 24,  0 )

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
  uint32_t range = 1 + max - min;
  val = (val * range) >> 8;
  return val + min;
}

// ====================================================================================================

void setup()
{
  initGame();
}

// ----------------------------------------------------------------------------------------------------

void loop()
{
  millisByte = millis() >> 5;
  
  // Advance the random number every tick, even if we don't use it
  randGetByte();
  
  readFaceValues();

  switch (gameState)
  {
    case GameState_Init:    loopInit();     break;
    case GameState_Descend: loopDescend();  break;
    case GameState_Play:    loopPlay();     break;
    case GameState_Lose:    loopLose();     break;
    case GameState_Win:     loopWin();      break;
  }

  updateFaceValues();

  render();

  // Consume click if not already
  buttonSingleClicked();
}

// ----------------------------------------------------------------------------------------------------
// INIT

void initGame()
{
  gameState = GameState_Init;
  currentRoom = null;
  tileRole = TileRole_Init;

  playerHitPoints = 3;
}

void loopInit()
{
  //if (tileRole == TileRole_Init)
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

  // Reset some player state for the new level
  playerFace = 0;
  playerHasKey = false;
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

  // Did the player lose all her hit points?
  if (playerHitPoints == 0)
  {
    loseGame();
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

  checkItemCollisions();
  checkMonsterCollisions();
}

void checkItemCollisions()
{
  // Check if the player stepped on an item

  // If no items in this room then do nothing
  if (currentRoom->gameplay.itemType == ItemType_None)
  {
    return;
  }

  // If player is not standing on the item then do nothing
  if (currentRoom->gameplay.itemFace != playerFace)
  {
    return;
  }

  switch (currentRoom->gameplay.itemType)
  {
    case ItemType_Key:
      playerHasKey = true;
      currentRoom->gameplay.itemType = ItemType_None;
      break;

    case ItemType_LockedDoor:
      if (playerHasKey)
      {
        currentRoom->gameplay.itemType = ItemType_OpenDoor;
      }
      break;

    case ItemType_Exit:
      winGame();
      break;
  }
}

void checkMonsterCollisions()
{
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
          currentRoom->gameplay.monsterType = MonsterType_None;
        }
      }
      break;
  }
  
  if (!backgroundPulseTimer.isExpired() && pulseReason == PulseReason_PlayerDamaged)
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
      if (currentRoom->faceValue.roomPresent &&
          currentRoom->faceValue.moveInfo)
      {
        tryToMoveHere = !tryToMoveHere;
      }
    }
  }
}

// ----------------------------------------------------------------------------------------------------
// LOSE

void loseGame()
{
  gameState = GameState_Lose;

  // Reuse these for coloring during death
  playerHitPoints = 31;
  playerMoveTimer.set(LOSE_GAME_FADE_RATE);
}

void loopLose()
{
  if (playerMoveTimer.isExpired())
  {
    playerHitPoints--;
    if (playerHitPoints < (tileRole == TileRole_Player ? 8 : 1))
    {
      initGame();
      return;
    }
    playerMoveTimer.set(LOSE_GAME_FADE_RATE);
  }
}

// ----------------------------------------------------------------------------------------------------
// WIN

void winGame()
{
  gameState = GameState_Win;

  // Reuse these for coloring during death
  playerHitPoints = 31;
  playerMoveTimer.set(LOSE_GAME_FADE_RATE);
}

void loopWin()
{
  if (playerMoveTimer.isExpired())
  {
    playerHitPoints--;
    if (playerHitPoints < (tileRole == TileRole_Player ? 8 : 1))
    {
      initGame();
      return;
    }
    playerMoveTimer.set(LOSE_GAME_FADE_RATE);
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
        if (val.faceValue.moveInfo == 1)
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

        GameState newGameState = (GameState) val.faceValue.gameState;
        if (newGameState == GameState_Lose && gameState != GameState_Lose)
        {
          loseGame();
        }
        if (newGameState == GameState_Win && gameState != GameState_Win)
        {
          winGame();
        }
        gameState = newGameState;

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
    if (tileRole == TileRole_Player)
    {
      // If we're the player then transmit the adjacent room info
      bool showPlayer = false;
      if (currentRoom != null)
      {
        if (!isValueReceivedOnFaceExpired(f))
        {
          RoomCoord newCoord = nextCoord(currentRoom->gameplay.coord, f);
          RoomData *nextRoom = findRoom(newCoord);
          if (nextRoom == null)
          {
            // Non-existent room - output a filled space
            roomDataOut.faceValue.roomPresent = false;

            // This is to tell to render the room, not that we can move into it
            roomDataOut.faceValue.moveInfo = true;
          }
          else
          {
            // Room exists - output its info
            roomDataOut.faceValue = nextRoom->faceValue;

            // Flag if we should temporarily show the player in the adjacent room
            roomDataOut.faceValue.showPlayer = false;
            if (movingToNewRoom && f == playerFace)
            {
              showPlayer = true;
            }

            // Flag if movement is possible to the adjacent room
            roomDataOut.faceValue.moveInfo = true;
            if (currentRoom->gameplay.itemType == ItemType_LockedDoor &&
                currentRoom->gameplay.itemFace == f)
            {
              // Locked door blocks movement and visibility
              roomDataOut.faceValue.moveInfo = false;
            }
          }
        }
      }

      roomDataOut.faceValue.showPlayer = showPlayer;
      roomDataOut.faceValue.fromFace = f;
      roomDataOut.faceValue.gameState = gameState;
      roomDataOut.faceValue.toggle = (toggleMask >> f) & 0x1;
    }
    else if (tileRole == TileRole_Adjacent)
    {
      roomDataOut.faceValue.moveInfo = tryToMoveHere;
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

  // From the starting room there should be an open path to the key
  levelRoomData[0].levelGen.keyPath = true;

  while (maxRoomData < MAX_ROOMS)
  {
    // Loop through all the rooms added in the previous pass
    byte thisPassStartIndex = maxRoomData;

    for (byte roomIndex = prevFullPassStartIndex; roomIndex < thisPassStartIndex; roomIndex++)
    {
      RoomData *prevRoomData = &levelRoomData[roomIndex];
      RoomTemplate *roomTemplate = &roomTemplates[prevRoomData->levelGen.templateIndex];
      byte entryFace = prevRoomData->levelGen.entryFace;

      byte nextRoomIndex = maxRoomData;   // used to track how many new rooms were added
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
            // Room was not generated
            // Either filled the room data array or encountered a previously-generated room
          }
        }
      }

      // Propagate the 'keyPath' flag to one of its exits
      if (prevRoomData->levelGen.keyPath)
      {
        if (maxRoomData > nextRoomIndex)
        {
          // Generated at least one exit - choose one to propagate the keyPath
          byte keyPathIndex = randRange(nextRoomIndex, maxRoomData - 1);
          levelRoomData[keyPathIndex].levelGen.keyPath = true;
        }
      }
    }

    // If we get through a pass without generating any new rooms then we're done
    if (thisPassStartIndex == maxRoomData)
    {
      break;
    }
  }

  // ===============================================================
  // All rooms generated - post process to insert items and monsters
  // This is the process that transforms the 'levelGen' room data into 'gameplay' room data.
  // Need to make sure we do things in the correct order so that levelGen bits aren't overwritten before they are used.

  // ---------------------------------------------------------------
  // KEY
  // The key is placed along the 'keyPath', which are flags for rooms where we guarantee no doors will be placed
  for (char roomIndex = maxRoomData - 1; roomIndex >= 0; roomIndex--)
  {
    RoomData *roomData = &levelRoomData[roomIndex];
    if (roomData->levelGen.keyPath)
    {
      // Place the key and break out - only one key in each level
      roomData->gameplay.itemType = ItemType_Key;
      break;
    }
  }

  // ---------------------------------------------------------------
  // EXIT
  // The exit is placed far away from the start and not on the 'keyPath'.
  // It also must have only a single exit.
  // TODO : Handle corner case when there are no rooms with a single exit!
  for (char roomIndex = maxRoomData - 1; roomIndex >= 0; roomIndex--)
  {
    RoomData *roomData = &levelRoomData[roomIndex];
    if (!roomData->levelGen.keyPath && roomData->gameplay.itemType == ItemType_None)
    {
      if (numNeighborRooms(roomData) == 1)
      {
        // Place the exit
        roomData->gameplay.itemType = ItemType_Exit;

        // Place the door at the entrance of this room
        FOREACH_FACE(f)
        {
          RoomCoord neighborCoord = nextCoord(roomData->levelGen.coord, f);
          RoomData *neighborRoom = findRoom(neighborCoord);
          if (neighborRoom != null)
          {
            neighborRoom->gameplay.itemType = ItemType_LockedDoor;
            neighborRoom->gameplay.itemFace = OPPOSITE_FACE(f);
            break;
          }
        }

        // Break out - only one exit and door in each level
        break;
      }
    }
  }

  for (byte roomIndex = 0; roomIndex < maxRoomData; roomIndex++)
  {
      RoomData *roomData = &levelRoomData[roomIndex];

      roomData->gameplay.TMP_KeyPath = roomData->levelGen.keyPath;

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

    // No new room was generated
    return false;
  }

  // Got here so room does not already exist - try to create it

  // Check if room data array is full
  if (maxRoomData >= MAX_ROOMS)
  {
    // No new room was generated
    return false;
  }

  // There's space for this room - allocate it
  roomData = &levelRoomData[maxRoomData];
  maxRoomData++;

  // Zero all bits so that unused fields are clear
  roomData->rawBits = 0;

  // Set the bit to say this isn't a solid wall
  roomData->levelGen.roomPresent = true;

  roomData->levelGen.coord = coord;

  // Retain the entry face for the next pass
  // It informs which faces can contain exits
  roomData->levelGen.entryFace = entryFace;

  // Copy the template info
  roomData->levelGen.templateIndex = templateIndex;
  roomData->levelGen.monsterClass = roomTemplate->monsterClass;
  roomData->levelGen.itemClass = roomTemplate->itemClass;

  // New room was generated!
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

byte numNeighborRooms(RoomData *roomData)
{
  byte count = 0;
  FOREACH_FACE(f)
  {
    RoomCoord neighborCoord = nextCoord(roomData->levelGen.coord, f);
    RoomData *neighborRoom = findRoom(neighborCoord);
    if (neighborRoom != null)
    {
      count++;
    }
  }
  return count;
}

// ====================================================================================================

void renderRoom(RoomData *roomData)
{
  Color color;

  // Draw the room walls and empty spaces
  FOREACH_FACE(f)
  {
#if DEBUG_SHOW_KEYPATH
    color.as_uint16 = (roomData->gameplay.roomPresent) ? (roomData->gameplay.TMP_KeyPath ? COLOR_KEYPATH : COLOR_EMPTY) : COLOR_WALL;
#else
    color.as_uint16 = (roomData->gameplay.roomPresent) ? COLOR_EMPTY : COLOR_WALL;
#endif

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
      // Don't display the room if movement is blocked (locked door in the way)
      if (!roomData->faceValue.moveInfo)
      {
        return;
      }
      
      if (tryToMoveHere)
      {
        // TODO : Better light up algorithm
        uint16_t a = (color.as_uint16 << 1) & 0b1111011110111100;
        color.as_uint16 |= a;//((color.as_uint16 >> 1) & 0b0111101111011110) + 0b01000100010000;
      }
    }
    setColorOnFace(color, f);
  }

  // Draw monsters & items
  if (roomData->gameplay.roomPresent)
  {
    if (roomData->gameplay.itemType != ItemType_None)
    {
      switch (roomData->gameplay.itemType)
      {
        case ItemType_LockedDoor: color.as_uint16 = COLOR_DOOR; break;
        case ItemType_Key:
          color.as_uint16 = (millisByte & 0x1) ? COLOR_KEY1 : COLOR_KEY2;
          break;
        case ItemType_Exit: color.as_uint16 = COLOR_EXIT; break;
      }

      byte face = roomData->gameplay.itemFace;
      face = CW_FROM_FACE(face, relativeRotation);
      setColorOnFace(color, face);

      // Some items affect other faces
      if (roomData->gameplay.itemType == ItemType_LockedDoor)
      {
        // Dim by half
        color.as_uint16 = COLOR_DOOR >> 1;
        color.as_uint16 &= 0b0111101111011110;
        face = CW_FROM_FACE(face, 1);
        setColorOnFace(color, face);
        face = CW_FROM_FACE(face, 4);
        setColorOnFace(color, face);
      }
      else if (roomData->gameplay.itemType == ItemType_Exit)
      {
        for (byte i = 0; i < 3; i++)
        {
          color.as_uint16 = color.as_uint16 >> 1;
          color.as_uint16 &= 0b0111101111011110;
          face = CW_FROM_FACE(face, 1);
          setColorOnFace(color, face);
        }
      }
    }

    if (roomData->gameplay.monsterType == MonsterType_Rat)
    {
      color.as_uint16 = roomData->gameplay.monsterState == MonsterState_Rat_Bite ? COLOR_DANGER : COLOR_MONSTER_RAT;
      byte face = roomData->gameplay.monsterFace;
      face = CW_FROM_FACE(face, relativeRotation);
      setColorOnFace(color, face);
    }
  }

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

void render()
{
  Color color;

  setColor(OFF);

  if (gameState == GameState_Init)
  {
    color.as_uint16 = COLOR_WALL;
    setColor(color);
    return;
  }
  
  if (gameState == GameState_Lose)
  {
    color.r = playerHitPoints;
    color.g = color.b = 0;
    setColor(color);
    return;
  }

  if (gameState == GameState_Win)
  {
    color.g = playerHitPoints;
    color.r = color.b = 0;
    setColor(color);
    return;
  }

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
