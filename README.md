# Character Dialogue Game

A small C game where you choose characters, follow branching conversations, and collect evidence.

## Tools and versions

- Visual Studio Code 1.142.0
- raylib 6.0 (included in the `raylib` folder)

## Build and run

Open the project folder in VS Code. Press **Ctrl+Shift+B** to run the **Build game** task. It creates `test.exe` in the project folder.

Run `.\test.exe` in the VS Code terminal, with the project folder as the current directory so the game can find its dialogue files.

## Dialogue and inventory

Dialogue is stored in `DialogueText1.txt` through `DialogueText5.txt`. To award evidence when a dialogue line is reached, add `EVIDENCE:<identifier>` after the three quoted strings on that line. For example:

```text
0 0 1 1 1 "Witness statement.">"Tell me more.">"Goodbye." EVIDENCE:witness_statement
```
## Incomplete

The game 'story' is still not implemented. The dialogue text is currently just placeholder.

Collected evidence is saved in `Inventory.txt`. In the game, choose **Reset Save** and then **Confirm Reset** to clear it.
(Written in assistance with Co-Pilot)
