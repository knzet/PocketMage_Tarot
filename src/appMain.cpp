// @knzet 2025

#include <pocketmage.h>
#include <vector>

#define TOTAL_CARDS 22

static std::vector<int> totalDrawnCards; // Keeps track of indices already drawn

static constexpr const char *TAG = "TAROT";
static volatile bool alreadyDrawnThisEinkPage = true;
static volatile bool didRenderWelcomeMessage = false;
const char *majorArcana[] = {
    "The Fool", "The Magician", "The High Priestess", "The Empress", "The Emperor",
    "The Hierophant", "The Lovers", "The Chariot", "Strength", "The Hermit",
    "Wheel of Fortune", "Justice", "The Hanged Man", "Death", "Temperance",
    "The Devil", "The Tower", "The Star", "The Moon", "The Sun",
    "Judgement", "The World"};

int x = 0;
bool firstRun = true;
char inchar;
String cardName;
String cardNamesThisSpread;
static volatile int CARDS_PER_PAGE = 3;

int selectedCard = -1;
bool tarotLoaded = false;

static int pageCardIdx[3];

bool drawTarotToBuffer(int idx, int nCardSpread, int drawnCards)
{
  char path[32];
  snprintf(path, sizeof(path), "/assets/tarot/%d/ar%02d.bin", nCardSpread, idx);

  int CARD_W;
  int CARD_H;

  int cardX;
  int cardY;

  switch (nCardSpread)
  {
  case 3:
  {
    CARD_W = 88;
    CARD_H = 153;
    // center one should be centered horizontally, other 2 should be offset from the center
    cardX = ((display.width() - CARD_W) / 2) + ((drawnCards - 1) * (CARD_W + 10)); // 10px horizontal padding
    // centered vertically for 1 or 3 cards
    cardY = (display.height() - CARD_H) / 2;
    break;
  }
    // todo: fix jump to case error here to allow it
  case 1:
  default:
  {
    CARD_W = 128;
    CARD_H = 218;
    // centered horizontally and vertically
    cardX = (display.width() - CARD_W) / 2;
    cardY = (display.height() - CARD_H) / 2;
    break;
  }
  };

  const int BYTES = CARD_W * CARD_H / 8;
  uint8_t tarotImage[BYTES];
  static volatile bool sdActive = true;
  // OLED().setSD(&sdActive);
  if (!SD().readBinaryFile(path, tarotImage, BYTES))
  {
    ESP_LOGI(TAG, "ERR: Failed to read %s\n", path);

    OLED().oledWord("SD Read Error %s", path);
    return false;
  }
  sdActive = false;

  // display.setRotation(3);
  // display.setFullWindow();
  // display.setTextColor(GxEPD_BLACK);

  // display.drawRect(cardX, cardY, CARD_W, CARD_H, GxEPD_BLACK);
  display.drawBitmap(cardX, cardY, tarotImage, CARD_W, CARD_H, GxEPD_BLACK, GxEPD_WHITE);
  String msg = String(idx) + " - " + cardName;
  // OLED().oledWord(msg);

  return true;
}

// ADD PROCESS/KEYBOARD APP SCRIPTS HERE
void processKB()
{
  inchar = KB().updateKeypress();
  // EINK().setFullRefreshAfter(1);

  if (inchar == 21)
  {
    // right arrow - 3 cards
    CARDS_PER_PAGE = 3;
    alreadyDrawnThisEinkPage = false;
  }
  else if (inchar == 20)
  {
    // SEL button - 1 card
    CARDS_PER_PAGE = 1;
    alreadyDrawnThisEinkPage = false;
  }
  else if (inchar == 19)
  {
    // reshuffle deck
    totalDrawnCards.clear();
    OLED().oledWord("Shuffled", true);
    didRenderWelcomeMessage = false;
  }
  else if (inchar != 0)
  {
    // Return to pocketMage OS
    // EINK().setFullRefreshAfter(FULL_REFRESH_AFTER);

    rebootToPocketMage();
  }

  delay(10);
}
void showTarotSplash()
{
  display.setRotation(3);
  display.setFullWindow();
  display.setTextColor(GxEPD_BLACK);

  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);

    // Title
    display.setTextSize(2);
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds("TAROT", 0, 0, &x1, &y1, &w, &h);
    display.setCursor((display.width() - w) / 2, 50);
    display.print("TAROT");

    // Subtitle
    display.setTextSize(1);
    const char *subtitle = "Major Arcana";
    display.getTextBounds(subtitle, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((display.width() - w) / 2, 85);
    display.print(subtitle);

    // Divider line
    display.drawLine(40, 100, display.width() - 40, 100, GxEPD_BLACK);

    // Instructions
    display.setCursor(30, 130);
    display.print("O  = draw 1 card");

    display.setCursor(30, 145);
    display.print(">  = draw 3 cards");

    display.setCursor(30, 160);
    display.print("<  = reshuffle deck");

    display.setCursor(30, 185);
    display.print("Any key = exit");

  } while (display.nextPage());
}

void applicationEinkHandler()
{
  if (!didRenderWelcomeMessage)
  {
    // todo: eink greeting
    showTarotSplash();
    if (firstRun)
    {
      OLED().oledWord("", false, true);
      firstRun = false;
    }
    // OLED().oledWord("o to draw 1, > to draw 3, < to shuffle, any key to exit", false);
    didRenderWelcomeMessage = true;
  }
  if (!alreadyDrawnThisEinkPage)
  {
    alreadyDrawnThisEinkPage = true;
    int cardsDrawnThisPage = 0;
    cardNamesThisSpread = "";
    display.setRotation(3);
    display.setFullWindow();
    display.setTextColor(GxEPD_BLACK);

    OLED().oledWord(
        CARDS_PER_PAGE == 3 ? "Drawing 3 cards..." : "Drawing a card...");

    for (int drawnCards = 0; drawnCards < CARDS_PER_PAGE; drawnCards++)
    {
      static const int MAJOR_ARCANA_COUNT = sizeof(majorArcana) / sizeof(majorArcana[0]);
      // Pick a random card at app start and when user draws another
      static const int TAROT_COUNT = sizeof(majorArcana) / sizeof(majorArcana[0]);
      if (totalDrawnCards.size() >= TOTAL_CARDS)
      {
        ESP_LOGI(TAG, "Listing directory %s\r\n", dirname);
        OLED().oledWord("End of deck reached!", true);
        return;
      }

      int idx;
      do
      {
        idx = esp_random() % TOTAL_CARDS;
      } while (std::find(totalDrawnCards.begin(), totalDrawnCards.end(), idx) != totalDrawnCards.end());
      totalDrawnCards.push_back(idx); // Mark this card as drawn so no duplicates

      pageCardIdx[drawnCards] = idx;

      cardName = majorArcana[idx];

      ESP_LOGI(TAG, "cardname: %s\r\n", cardName);
      // track names to render to oled
      cardNamesThisSpread.concat(cardName + " ");
    }

    // display.firstPage();
    // do
    // {
    display.fillScreen(GxEPD_WHITE);
    // draw cards evenly across the page, n cards in a spread
    for (int drawnCards = 0; drawnCards < CARDS_PER_PAGE; drawnCards++)
    {
      drawTarotToBuffer(pageCardIdx[drawnCards], CARDS_PER_PAGE, drawnCards);
    }

    // } while (display.nextPage());
    // EINK().refresh();
    EINK().forceSlowFullUpdate(true);
    EINK().refresh();
    // EINK().multiPassRefresh(2);

    OLED().oledWord(cardNamesThisSpread);
  } // end if
}

/////////////////////////////////////////////////////////////
//  ooo        ooooo       .o.       ooooo ooooo      ooo  //
//  `88.       .888'      .888.      `888' `888b.     `8'  //
//   888b     d'888      .8"888.      888   8 `88b.    8   //
//   8 Y88. .P  888     .8' `888.     888   8   `88b.  8   //
//   8  `888'   888    .88ooo8888.    888   8     `88b.8   //
//   8    Y     888   .8'     `888.   888   8       `888   //
//  o8o        o888o o88o     o8888o o888o o8o        `8   //
/////////////////////////////////////////////////////////////
// SETUP

void setup()
{
  PocketMage_INIT();
}

void loop()
{
  // Check battery
  pocketmage::power::updateBattState();

  // Run KB loop
  processKB();

  // Yield to watchdog
  vTaskDelay(50 / portTICK_PERIOD_MS);
  yield();
}

// migrated from einkFunc.cpp
void einkHandler(void *parameter)
{
  vTaskDelay(pdMS_TO_TICKS(250));
  for (;;)
  {
    applicationEinkHandler();

    vTaskDelay(pdMS_TO_TICKS(50));
    yield();
  }
}
