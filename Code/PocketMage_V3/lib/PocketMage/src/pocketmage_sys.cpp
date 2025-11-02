//  888888ba                    dP                  dP                                          //
//  88    `8b                   88                  88                                          //
// a88aaaa8P' .d8888b. .d8888b. 88  .dP  .d8888b. d8888P 88d8b.d8b. .d8888b. .d8888b. .d8888b.  //
//  88        88'  `88 88'  `"" 88888"   88ooood8   88   88'`88'`88 88'  `88 88'  `88 88ooood8  //
//  88        88.  .88 88.  ... 88  `8b. 88.  ...   88   88  88  88 88.  .88 88.  .88 88.  ...  //
//  dP        `88888P' `88888P' dP   `YP `88888P'   dP   dP  dP  dP `88888P8 `8888P88 `88888P'  //
//                                                                                .88           //
//                                                                            d8888P            //

#include <globals.h>
#include <config.h>
#include <RTClib.h>
#include <SD_MMC.h>
#include <Preferences.h>
#include <esp_log.h>
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_system.h"

static constexpr const char* TAG = "SYSTEM";
// To Do: migrate to pocketmage::

///////////////////////////////////////////////////////////////////////////////
//            Use this function in apps to return to PocketMage OS           //
bool rebootToPocketMage() {
    const esp_partition_t *partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                 ESP_PARTITION_SUBTYPE_APP_OTA_0, // instead of FACTORY
                                 nullptr);
    if (!partition) {
        Serial.println("OTA0 partition not found");
        return false;
    }

    esp_err_t err = esp_ota_set_boot_partition(partition);
    if (err != ESP_OK) {
        Serial.printf("esp_ota_set_boot_partition failed: %d\n", (int)err);
        return false;
    }

    Serial.println("Boot partition set to OTA0 (PocketMage OS). Restarting...");
    esp_restart();
    return true;
}
///////////////////////////////////////////////////////////////////////////////

volatile bool PWR_BTN_event = false;
bool noTimeout = false;               // Disable timeout
bool mscEnabled         = false;
bool sinkEnabled        = false;
volatile bool SDActive  = false;
volatile int battState = 0;           // Bary state


void PocketMage_INIT(){
  // Serial, I2C, SPI
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);
  SPI.begin(SPI_SCK, -1, SPI_MOSI, -1);

  // OLED SETUP
  setupOled();

  // STARTUP JINGLE
  setupBZ();
  
  // WAKE INTERRUPT SETUP
  pinMode(KB_IRQ, INPUT);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_8, 0);

  // KEYBOARD SETUP
  setupKB(KB_IRQ);

  // EINK HANDLER SETUP
  setupEink();
  
  // SD CARD SETUP
  setupSD();

  // POWER SETUP
  pinMode(PWR_BTN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PWR_BTN), pocketmage::power::PWR_BTN_irq, FALLING);
  pinMode(CHRG_SENS, INPUT);
  pinMode(BAT_SENS, INPUT);
  if (!PowerSystem.init(I2C_SDA, I2C_SCL)) {
    ESP_LOGV(TAG, "MP2722 Failed to Init");
  }
  //WiFi.mode(WIFI_OFF);
  //btStop();

  // SET CPU CLOCK FOR POWER SAVE MODE
  if (SAVE_POWER) setCpuFrequencyMhz(40 );
  else            setCpuFrequencyMhz(240);
  
  // CAPACATIVE TOUCH SETUP
  setupTouch();

  // RTC SETUP
  setupClock();

  // Set "random" seed
  randomSeed(analogRead(BAT_SENS));

  // Load State
  loadState();
}

// Helpers
static int countVisibleChars(String input) {
  int count = 0;

  for (size_t i = 0; i < input.length(); i++) {
    char c = input[i];
    // Check if the character is a visible character or space
    if (c >= 32 && c <= 126) {  // ASCII range for printable characters and space
      count++;
    }
  }

  return count;
}

namespace pocketmage::file{
    
    void saveFile() {
    if (SD().getNoSD()) {
        OLED().oledWord("SAVE FAILED - No SD!");
        delay(5000);
        return;
    } else {
        SDActive = true;
        setCpuFrequencyMhz(240);
        delay(50);

        String textToSave = vectorToString();
        ESP_LOGV(TAG, "Text to save: %s", textToSave.c_str());

        if (SD().getEditingFile() == "" || SD().getEditingFile() == "-")
        SD().setEditingFile("/temp.txt");
        keypad.disableInterrupts();
        if (!SD().getEditingFile().startsWith("/"))
        SD().setEditingFile("/" + SD().getEditingFile());
        //OLED().oledWord("Saving File: "+ editingFile);
        SD().writeFile(SD_MMC, (SD().getEditingFile()).c_str(), textToSave.c_str());
        //OLED().oledWord("Saved: "+ editingFile);

        // Write MetaData
        pocketmage::file::writeMetadata(SD().getEditingFile());

        // delay(1000);
        keypad.enableInterrupts();
        if (SAVE_POWER)
        setCpuFrequencyMhz(POWER_SAVE_FREQ);
        SDActive = false;
    }
    }
    
    void writeMetadata(const String& path) {
    SDActive = true;
    setCpuFrequencyMhz(240);
    delay(50);

    File file = SD_MMC.open(path);
    if (!file || file.isDirectory()) {
        OLED().oledWord("META WRITE ERR");
        delay(1000);
        ESP_LOGE(TAG, "Invalid file for metadata: %s", path);
        return;
    }
    // Get file size
    size_t fileSizeBytes = file.size();
    file.close();

    // Format size string
    String fileSizeStr = String(fileSizeBytes) + " Bytes";

    // Get line and char counts
    int charCount = countVisibleChars(SD().readFileToString(SD_MMC, path.c_str()));

    String charStr = String(charCount) + " Char";
    // Get current time from RTC
    DateTime now = CLOCK().nowDT();
    char timestamp[20];
    sprintf(timestamp, "%04d%02d%02d-%02d%02d", now.year(), now.month(), now.day(), now.hour(),
            now.minute());

    // Compose new metadata line
    String newEntry = path + "|" + timestamp + "|" + fileSizeStr + "|" + charStr;

    const char* metaPath = SYS_METADATA_FILE;
    // Read existing entries and rebuild the file without duplicates
    File metaFile = SD_MMC.open(metaPath, FILE_READ);
    String updatedMeta = "";
    bool replaced = false;

    if (metaFile) {
        while (metaFile.available()) {
        String line = metaFile.readStringUntil('\n');
        if (line.startsWith(path + "|")) {
            updatedMeta += newEntry + "\n";
            replaced = true;
        } else if (line.length() > 1) {
            updatedMeta += line + "\n";
        }
        }
        metaFile.close();
    }

    if (!replaced) {
        updatedMeta += newEntry + "\n";
    }
    // Write back the updated metadata
    metaFile = SD_MMC.open(metaPath, FILE_WRITE);
    if (!metaFile) {
        ESP_LOGE(TAG, "Failed to open metadata file for writing: %s", metaPath);
        return;
    }
    metaFile.print(updatedMeta);
    metaFile.close();
    ESP_LOGI(TAG, "Metadata updated");

    if (SAVE_POWER)
    setCpuFrequencyMhz(POWER_SAVE_FREQ);
    SDActive = false;
    }
    
    void loadFile(bool showOLED) {
    SDActive = true;
    setCpuFrequencyMhz(240);
    delay(50);

    if (SD().getNoSD()) {
        OLED().oledWord("LOAD FAILED - No SD!");
        delay(5000);
        return;
    } else {
        SDActive = true;
        setCpuFrequencyMhz(240);
        delay(50);

        keypad.disableInterrupts();
        if (showOLED)
        OLED().oledWord("Loading File");
        if (!SD().getEditingFile().startsWith("/"))
        SD().setEditingFile("/" + SD().getEditingFile());
        String textToLoad = SD().readFileToString(SD_MMC, (SD().getEditingFile()).c_str());
        ESP_LOGV(TAG, "Text to load: %s", textToLoad.c_str());

        stringToVector(textToLoad);
        keypad.enableInterrupts();
        if (showOLED) {
        OLED().oledWord("File Loaded");
        delay(200);
        }
        if (SAVE_POWER)
        setCpuFrequencyMhz(POWER_SAVE_FREQ);
        SDActive = false;
    }
    }
    
    void delFile(String fileName) {
    if (SD().getNoSD()) {
        OLED().oledWord("DELETE FAILED - No SD!");
        delay(5000);
        return;
    } else {
        SDActive = true;
        setCpuFrequencyMhz(240);
        delay(50);

        keypad.disableInterrupts();
        // OLED().oledWord("Deleting File: "+ fileName);
        if (!fileName.startsWith("/"))
        fileName = "/" + fileName;
        SD().deleteFile(SD_MMC, fileName.c_str());
        // OLED().oledWord("Deleted: "+ fileName);

        // Delete MetaData
        pocketmage::file::deleteMetadata(fileName);

        delay(1000);
        keypad.enableInterrupts();
        if (SAVE_POWER)
        setCpuFrequencyMhz(POWER_SAVE_FREQ);
        SDActive = false;
    }
    }
    
    void deleteMetadata(String path) {
    SDActive = true;
    setCpuFrequencyMhz(240);
    delay(50);

    const char* metaPath = SYS_METADATA_FILE;

    // Open metadata file for reading
    File metaFile = SD_MMC.open(metaPath, FILE_READ);
    if (!metaFile) {
        ESP_LOGE(TAG, "Metadata file not found: %s", metaPath);
        return;
    }

    // Store lines that don't match the given path
    std::vector<String> keptLines;
    while (metaFile.available()) {
        String line = metaFile.readStringUntil('\n');
        if (!line.startsWith(path + "|")) {
        keptLines.push_back(line);
        }
    }
    metaFile.close();

    // Delete the original metadata file
    SD_MMC.remove(metaPath);

    // Recreate the file and write back the kept lines
    File writeFile = SD_MMC.open(metaPath, FILE_WRITE);
    if (!writeFile) {
        ESP_LOGE(TAG, "Failed to recreate metadata file. %s", writeFile.path());
        return;
    }

    for (const String& line : keptLines) {
        writeFile.println(line);
    }

    writeFile.close();
    ESP_LOGI(TAG, "Metadata entry deleted (if it existed).");
    }
    
    void renFile(String oldFile, String newFile) {
    if (SD().getNoSD()) {
        OLED().oledWord("RENAME FAILED - No SD!");
        delay(5000);
        return;
    } else {
        SDActive = true;
        setCpuFrequencyMhz(240);
        delay(50);

        keypad.disableInterrupts();
        // OLED().oledWord("Renaming "+ oldFile + " to " + newFile);
        if (!oldFile.startsWith("/"))
        oldFile = "/" + oldFile;
        if (!newFile.startsWith("/"))
        newFile = "/" + newFile;
        SD().renameFile(SD_MMC, oldFile.c_str(), newFile.c_str());
        OLED().oledWord(oldFile + " -> " + newFile);
        delay(1000);

        // Update MetaData
        pocketmage::file::renMetadata(oldFile, newFile);

        keypad.enableInterrupts();
        if (SAVE_POWER)
        setCpuFrequencyMhz(POWER_SAVE_FREQ);
        SDActive = false;
    }
    }
    
    void renMetadata(String oldPath, String newPath) {
    SDActive = true;
    setCpuFrequencyMhz(240);
    delay(50);
    const char* metaPath = SYS_METADATA_FILE;

    // Open metadata file for reading
    File metaFile = SD_MMC.open(metaPath, FILE_READ);
    if (!metaFile) {
        ESP_LOGE(TAG, "Metadata file not found: %s", metaPath);
        return;
    }

    std::vector<String> updatedLines;

    while (metaFile.available()) {
        String line = metaFile.readStringUntil('\n');
        if (line.startsWith(oldPath + "|")) {
        // Replace old path with new path at the start of the line
        int separatorIndex = line.indexOf('|');
        if (separatorIndex != -1) {
            // Keep rest of line after '|'
            String rest = line.substring(separatorIndex);
            line = newPath + rest;
        } else {
            // Just replace whole line with new path if malformed
            line = newPath;
        }
        }
        updatedLines.push_back(line);
    }

    metaFile.close();

    // Delete old metadata file
    SD_MMC.remove(metaPath);

    // Recreate file and write updated lines
    File writeFile = SD_MMC.open(metaPath, FILE_WRITE);
    if (!writeFile) {
        ESP_LOGE(TAG, "Failed to recreate metadata file. %s", writeFile.path());
        return;
    }

    for (const String& l : updatedLines) {
        writeFile.println(l);
    }

    writeFile.close();
    ESP_LOGI(TAG, "Metadata updated for renamed file.");

    if (SAVE_POWER)
        setCpuFrequencyMhz(POWER_SAVE_FREQ);
    }
    
    void copyFile(String oldFile, String newFile) {
    if (SD().getNoSD()) {
        OLED().oledWord("COPY FAILED - No SD!");
        delay(5000);
        return;
    } else {
        SDActive = true;
        setCpuFrequencyMhz(240);
        delay(50);

        keypad.disableInterrupts();
        OLED().oledWord("Loading File");
        if (!oldFile.startsWith("/"))
        oldFile = "/" + oldFile;
        if (!newFile.startsWith("/"))
        newFile = "/" + newFile;
        String textToLoad = SD().readFileToString(SD_MMC, (oldFile).c_str());
        SD().writeFile(SD_MMC, (newFile).c_str(), textToLoad.c_str());
        OLED().oledWord("Saved: " + newFile);

        // Write MetaData
        pocketmage::file::writeMetadata(newFile);

        delay(1000);
        keypad.enableInterrupts();

        if (SAVE_POWER)
        setCpuFrequencyMhz(POWER_SAVE_FREQ);
        SDActive = false;
    }
    }
    
    void appendToFile(String path, String inText) {
    if (SD().getNoSD()) {
        OLED().oledWord("OP FAILED - No SD!");
        delay(5000);
        return;
    } else {
        SDActive = true;
        setCpuFrequencyMhz(240);
        delay(50);

        keypad.disableInterrupts();
        SD().appendFile(SD_MMC, path.c_str(), inText.c_str());

        // Write MetaData
        pocketmage::file::writeMetadata(path);

        keypad.enableInterrupts();

        if (SAVE_POWER)
        setCpuFrequencyMhz(POWER_SAVE_FREQ);
        SDActive = false;
    }
    }
}   // namespace pocketmage::file

namespace pocketmage::time{
    
    void setTimeFromString(String timeStr) {
    if (timeStr.length() != 5 || timeStr[2] != ':') {
        ESP_LOGE(TAG, "Invalid format! Use HH:MM. Provided str: %s", timeStr.c_str());
        return;
    }

    int hours = timeStr.substring(0, 2).toInt();
    int minutes = timeStr.substring(3, 5).toInt();

    if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59) {
        OLED().oledWord("Invalid");
        delay(500);
        return;
    }

    DateTime now = CLOCK().nowDT();  // Get current date
    CLOCK().getRTC().adjust(DateTime(now.year(), now.month(), now.day(), hours, minutes, 0));

    ESP_LOGI(TAG, "Time updated!");
    }
    
    void setCpuSpeed(int newFreq) {
    // Return early if the frequency is already set
    if (getCpuFrequencyMhz() == newFreq)
        return;

    int validFreqs[] = {240, 160, 80, 40, 20, 10};
    bool isValid = false;

    for (int i = 0; i < sizeof(validFreqs) / sizeof(validFreqs[0]); i++) {
        if (newFreq == validFreqs[i]) {
        isValid = true;
        break;
        }
    }

    if (isValid) {
        setCpuFrequencyMhz(newFreq);
        ESP_LOGI(TAG, "CPU Speed changed to: %d MHz", newFreq);
    }
    }
}    // namespace pocketmage::time

namespace pocketmage::power{
    
    void deepSleep(bool alternateScreenSaver) {
    // Put OLED to sleep
    u8g2.setPowerSave(1);

    // Stop the einkHandler task
    if (einkHandlerTaskHandle != NULL) {
        vTaskDelete(einkHandlerTaskHandle);
        einkHandlerTaskHandle = NULL;
    }

    // Shutdown Jingle
    BZ().playJingle(Jingles::Shutdown);

    if (alternateScreenSaver == false) {
        SDActive = true;
        setCpuFrequencyMhz(240);
        delay(50);

        // Check if there are custom screensavers
        File dir = SD_MMC.open("/assets/backgrounds");
        std::vector<String> binFiles;

        if (dir) {
            File file;
            while ((file = dir.openNextFile())) {
                String name = file.name();
                if (name.endsWith(".bin")) binFiles.push_back(name);
                file.close();
            }
            dir.close();
        }

        display.setFullWindow();

        // Use custom screensavers
        if (!binFiles.empty()) {
            int fileIndex = esp_random() % binFiles.size();
            String path = "/assets/backgrounds/" + binFiles[fileIndex];
            File f = SD_MMC.open(path);
            if (f) {
                static uint8_t buf[320 * 240]; // Declare as static to avoid stack overflow :D
                f.read(buf, sizeof(buf));
                f.close();

                // Show file
                display.drawBitmap(0, 0, buf, 320, 240, GxEPD_BLACK);
                display.setFont(&FreeMonoBold9pt7b);
                display.setTextColor(GxEPD_BLACK);
                display.setCursor(5, display.height()-5);
                display.print(binFiles[fileIndex].c_str());
            }
        }
        // Use standard screensavers
        else {
            int numScreensavers = sizeof(ScreenSaver_allArray) / sizeof(ScreenSaver_allArray[0]);
            int randomScreenSaver_ = esp_random() % numScreensavers;

            display.drawBitmap(0, 0, ScreenSaver_allArray[randomScreenSaver_], 320, 240, GxEPD_BLACK);
        }


        if (SAVE_POWER) setCpuFrequencyMhz(POWER_SAVE_FREQ);
        SDActive = false;

        EINK().multiPassRefresh(2);
    } else {
        // Display alternate screensaver
        EINK().forceSlowFullUpdate(true);
        EINK().refresh();
        delay(100);
    }

    // Put E-Ink to sleep
    display.hibernate();

    // Save last state
    prefs.begin("PocketMage", false);
    prefs.putInt("CurrentAppState", static_cast<int>(CurrentAppState));
    prefs.putString("editingFile", SD().getEditingFile());
    prefs.end();

    // Sleep the ESP32
    esp_deep_sleep_start();
    }
    
    void IRAM_ATTR PWR_BTN_irq() {
    PWR_BTN_event = true;
    }
    

    

}    // namespace pocketmage::power

// ===================== GLOBAL TEXT HELPERS =====================
volatile bool newLineAdded = true;           // New line added in TXT
std::vector<String> allLines;         // All lines in TXT

String vectorToString() {
String result;
EINK().setTXTFont(EINK().getCurrentFont());

for (size_t i = 0; i < allLines.size(); i++) {
    result += allLines[i];

    int16_t x1, y1;
    uint16_t charWidth, charHeight;
    display.getTextBounds(allLines[i], 0, 0, &x1, &y1, &charWidth, &charHeight);

    // Add newline only if the line doesn't fully use the available space
    if (charWidth < display.width() && i < allLines.size() - 1) {
    result += '\n';
    }
}

return result;
}

void stringToVector(String inputText) {
EINK().setTXTFont(EINK().getCurrentFont());
allLines.clear();
String currentLine_;

for (size_t i = 0; i < inputText.length(); i++) {
    char c = inputText[i];

    int16_t x1, y1;
    uint16_t charWidth, charHeight;
    display.getTextBounds(currentLine_, 0, 0, &x1, &y1, &charWidth, &charHeight);

    // Check if new line needed
    if ((c == '\n' || charWidth >= display.width() - 5) && !currentLine_.isEmpty()) {
    if (currentLine_.endsWith(" ")) {
        allLines.push_back(currentLine_);
        currentLine_ = "";
    } else {
        int lastSpace = currentLine_.lastIndexOf(' ');
        if (lastSpace != -1) {
        // Split line at last space
        String partialWord = currentLine_.substring(lastSpace + 1);
        currentLine_ = currentLine_.substring(0, lastSpace);
        allLines.push_back(currentLine_);
        currentLine_ = partialWord;  // Start new line with partial word
        } else {
        // No spaces, whole line is a single word
        allLines.push_back(currentLine_);
        currentLine_ = "";
        }
    }
    }

    if (c != '\n') {
    currentLine_ += c;
    }
}

// Push last line if not empty
if (!currentLine_.isEmpty()) {
    allLines.push_back(currentLine_);
}
}

String removeChar(String str, char character) {
String result = "";
for (size_t i = 0; i < str.length(); i++) {
    if (str[i] != character) {
    result += str[i];
    }
}
return result;
}

int stringToInt(String str) {
str.trim();  // Remove leading/trailing whitespace

if (str.length() == 0)
    return -1;

for (int i = 0; i < str.length(); i++) {
    if (!isDigit(str.charAt(i))) {
    return -1;  // Invalid character found
    }
}

return str.toInt();  // Safe to convert
}
