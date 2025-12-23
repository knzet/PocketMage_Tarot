# functionality
- select button (between arrows) to draw 1 card, 
- right arrow to draw 3 cards, 
- left arrow to shuffle, 
- any other key to exit back to home screen.

# install
## to install the app to Pocketmage (v1.1 and newer):
1. put tarot.tar on sd card/apps
2. install as normal:
   ```
   load -> enter -> a, b, c, or d -> enter -> s -> pick tarot with arrow keys -> enter -> fn -> left arrow
   ```

## to install the app to earlier PocketMage versions (<1.1):
1. clone or download+unzip repo, run python script:
   ```
   python3 images/fetch.py
   ```
2. move the generated .bin files from images/output/05_binary/ to the sd card under assets/tarot/
3. build the app with pio run or ctrl+shift+b in vscode
4. move sd card to pocketmage
   ```
   load -> enter -> a, b, c, or d -> enter -> s -> pick tarot with arrow keys -> enter -> fn -> left arrow
   ```
   then launch the app with a, b, c, or d -> enter


