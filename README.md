# 450ProgProj
Programming Project for COSC 450

## Building

In VS Code, run the **Build game** task (`Terminal: Run Build Task`). It uses
the bundled MinGW compiler and raylib library and writes `test.exe` in the
project folder. The raylib library and Windows system libraries must be linked;
compiling `test.c` by itself will produce undefined-reference errors.

## Dialogue evidence

Append `EVIDENCE:<identifier>` after the three quoted dialogue strings on any
dialogue line to add that evidence identifier to the player's Inventory when
the node is reached. For example:

```text
0 0 1 1 1 "Witness statement.">"Tell me more.">"Goodbye." EVIDENCE:witness_statement
```

Evidence is collected once per identifier and remains in the Inventory when
switching characters. The Inventory button is available on the character
selection and dialogue screens.

The inventory is saved to `Inventory.txt` in the game's working directory and
loaded when the game starts. On the Inventory screen, click **Reset Save** and
then **Confirm Reset** to erase the saved evidence.
 (written by co-pilot)