pip install raylib

import os
import re
from dataclasses import dataclass, field
from enum import Enum, auto
import pyray as pr

# Path constants
INVENTORY_SAVE_PATH = "Inventory.txt"
MAX_PATH_DEPTH = 4


class GameScreen(Enum):
    CHARACTER_SELECT = auto()
    DIALOGUE = auto()
    INVENTORY = auto()


@dataclass
class DialogueNode:
    branch: list[int] = field(default_factory=list)
    spoken_text: str = ""
    choices: list[str] = field(default_factory=list)
    evidence_id: str = ""


@dataclass
class DialogueState:
    path: list[int] = field(default_factory=lambda: [0] * MAX_PATH_DEPTH)
    depth: int = 0
    current_node_index: int = -1
    ended: bool = False


@dataclass
class NPC:
    name: str
    file_path: str
    color: pr.Color
    box: pr.Rectangle


def find_node_by_branch(dialogue: list[DialogueNode], path: list[int]) -> int:
    """Find the index of the dialogue node matching the player's current path."""
    for i, node in enumerate(dialogue):
        if len(node.branch) == len(path) and all(b == p for b, p in zip(node.branch, path)):
            return i
    return -1


def load_dialogue_file(file_path: str) -> list[DialogueNode]:
    """Parse dialogue lines and evidence tags using Python string operations."""
    nodes = []
    if not os.path.exists(file_path):
        pr.trace_log(pr.LOG_ERROR, f"Failed to open dialogue file: {file_path}")
        return nodes

    with open(file_path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            # Skip empty lines and comments
            if not line or line.startswith("#"):
                continue

            # Extract quoted dialogue texts using regex
            quoted_texts = re.findall(r'"([^"]*)"', line)
            if len(quoted_texts) < 3:
                continue

            # Extract all integer tokens before the first quoted string
            prefix = line[:line.find('"')].strip()
            numbers = [int(n) for n in prefix.split() if n.lstrip("-").isdigit()]

            # We expect 4 branch numbers plus 1 extra compatibility integer
            if len(numbers) < 5:
                continue

            branch = numbers[:MAX_PATH_DEPTH]

            # Look for optional EVIDENCE:id identifier at the end of the line
            evidence_id = ""
            if "EVIDENCE:" in line:
                ev_part = line.split("EVIDENCE:", 1)[1].strip()
                evidence_id = ev_part.split()[0] if ev_part else ""

            node = DialogueNode(
                branch=branch,
                spoken_text=quoted_texts[0],
                choices=[quoted_texts[1], quoted_texts[2]],
                evidence_id=evidence_id
            )
            nodes.append(node)

    return nodes


def reset_dialogue_state(state: DialogueState, dialogue: list[DialogueNode]) -> None:
    """Reset position to the opening line (path filled with zeros)."""
    state.path = [0] * MAX_PATH_DEPTH
    state.depth = 0
    state.ended = False
    state.current_node_index = find_node_by_branch(dialogue, state.path)
    if state.current_node_index == -1:
        state.ended = True


def advance_dialogue(state: DialogueState, dialogue: list[DialogueNode], choice_index: int) -> None:
    """Record the chosen answer and locate the target dialogue node."""
    if state.ended or state.depth >= MAX_PATH_DEPTH:
        state.ended = True
        return

    # Write choice right-to-left matching original branch numbering layout
    state.path[MAX_PATH_DEPTH - 1 - state.depth] = choice_index + 1
    state.depth += 1
    state.current_node_index = find_node_by_branch(dialogue, state.path)

    if state.current_node_index == -1:
        state.ended = True


def save_inventory(inventory: set[str]) -> bool:
    """Save unique evidence IDs directly to disk."""
    try:
        with open(INVENTORY_SAVE_PATH, "w", encoding="utf-8") as f:
            for item in inventory:
                f.write(f"{item}\n")
        return True
    except OSError as e:
        pr.trace_log(pr.LOG_ERROR, f"Failed to save inventory: {e}")
        return False


def load_inventory() -> set[str]:
    """Load collected evidence IDs into a Python set."""
    inventory = set()
    if not os.path.exists(INVENTORY_SAVE_PATH):
        return inventory

    try:
        with open(INVENTORY_SAVE_PATH, "r", encoding="utf-8") as f:
            for line in f:
                item = line.strip()
                # Validate identifiers (alphanumeric, hyphens, underscores)
                if item and re.match(r'^[A-Za-z0-9_-]+$', item):
                    inventory.add(item)
    except OSError as e:
        pr.trace_log(pr.LOG_ERROR, f"Failed to read inventory save file: {e}")

    return inventory


def collect_evidence(node: DialogueNode, inventory: set[str]) -> None:
    """Collect unique evidence ID and persist changes."""
    if not node.evidence_id:
        return

    if node.evidence_id not in inventory:
        inventory.add(node.evidence_id)
        if not save_inventory(inventory):
            pr.trace_log(pr.LOG_ERROR, "Evidence collected but save failed.")


def draw_choice_button(box: pr.Rectangle, text: str, hover: bool) -> None:
    bg = pr.SKYBLUE if hover else pr.LIGHTGRAY
    border = pr.BLUE if hover else pr.DARKGRAY
    pr.draw_rectangle_rounded(box, 0.12, 0, bg)
    pr.draw_rectangle_rounded_lines(box, 0.12, 0, border)
    pr.draw_text(text, int(box.x) + 16, int(box.y) + 12, 20, pr.BLACK)


def draw_top_button(box: pr.Rectangle, text: str) -> None:
    pr.draw_rectangle_rounded(box, 0.12, 0, pr.LIGHTGRAY)
    pr.draw_text(text, int(box.x) + 14, int(box.y) + 8, 20, pr.BLACK)


def draw_wrapped_text(text: str, x: int, y: int, font_size: int, max_width: int, max_height: int, color: pr.Color) -> None:
    """Wrap text to fit within given box boundaries."""
    words = text.split(' ')
    lines = []
    current_line = ""

    for word in words:
        test_line = f"{current_line} {word}".strip() if current_line else word
        if pr.measure_text(test_line, font_size) <= max_width:
            current_line = test_line
        else:
            if current_line:
                lines.append(current_line)
            current_line = word
    if current_line:
        lines.append(current_line)

    line_height = font_size + 5
    max_lines = max_height // line_height
    for i, line in enumerate(lines[:max_lines]):
        pr.draw_text(line, x, y + i * line_height, font_size, color)


def main():
    characters = [
        NPC("Sara", "DialogueText1.txt", pr.RED, pr.Rectangle(60, 80, 220, 170)),
        NPC("Miranda", "DialogueText2.txt", pr.BLUE, pr.Rectangle(330, 80, 220, 170)),
        NPC("Ash", "DialogueText3.txt", pr.GREEN, pr.Rectangle(60, 300, 220, 170)),
        NPC("Asper", "DialogueText4.txt", pr.PURPLE, pr.Rectangle(330, 300, 220, 170)),
        NPC("Fila", "DialogueText5.txt", pr.PURPLE, pr.Rectangle(600, 80, 220, 170))
    ]

    nodes: list[DialogueNode] = []
    state = DialogueState()
    inventory: set[str] = load_inventory()
    
    inventory_scroll = 0
    inventory_visible_items = 12
    confirm_inventory_reset = False
    active_character = -1
    screen = GameScreen.CHARACTER_SELECT
    previous_screen = GameScreen.CHARACTER_SELECT

    pr.init_window(960, 540, "Character Dialogue Select")
    pr.set_target_fps(60)

    while not pr.window_should_close():
        mouse_pos = pr.get_mouse_position()

        if pr.is_mouse_button_pressed(pr.MOUSE_BUTTON_LEFT):
            if screen == GameScreen.CHARACTER_SELECT:
                # Check character selection cards
                for i, char in enumerate(characters):
                    if pr.check_collision_point_rec(mouse_pos, char.box):
                        active_character = i
                        nodes = load_dialogue_file(char.file_path)
                        if nodes:
                            reset_dialogue_state(state, nodes)
                            if not state.ended and state.current_node_index >= 0:
                                collect_evidence(nodes[state.current_node_index], inventory)
                            screen = GameScreen.DIALOGUE
                        break

                # Inventory button top-right
                if pr.check_collision_point_rec(mouse_pos, pr.Rectangle(720, 20, 180, 40)):
                    previous_screen = screen
                    screen = GameScreen.INVENTORY

            elif screen == GameScreen.DIALOGUE:
                back_button = pr.Rectangle(720, 20, 180, 40)
                inventory_button = pr.Rectangle(520, 20, 180, 40)

                if pr.check_collision_point_rec(mouse_pos, back_button):
                    screen = GameScreen.CHARACTER_SELECT
                    active_character = -1
                    nodes.clear()
                    state = DialogueState()

                elif pr.check_collision_point_rec(mouse_pos, inventory_button):
                    previous_screen = screen
                    screen = GameScreen.INVENTORY

                elif not state.ended and state.current_node_index >= 0:
                    for i in range(2):
                        choice_box = pr.Rectangle(80, 260 + i * 90, 800, 54)
                        if pr.check_collision_point_rec(mouse_pos, choice_box):
                            advance_dialogue(state, nodes, i)
                            if not state.ended and state.current_node_index >= 0:
                                collect_evidence(nodes[state.current_node_index], inventory)
                            break

            elif screen == GameScreen.INVENTORY:
                back_button = pr.Rectangle(720, 20, 180, 40)
                reset_button = pr.Rectangle(500, 20, 180, 40)

                if pr.check_collision_point_rec(mouse_pos, back_button):
                    screen = previous_screen
                    confirm_inventory_reset = False

                elif pr.check_collision_point_rec(mouse_pos, reset_button):
                    if confirm_inventory_reset:
                        if save_inventory(set()):
                            inventory.clear()
                            inventory_scroll = 0
                        confirm_inventory_reset = False
                    else:
                        confirm_inventory_reset = True
                else:
                    confirm_inventory_reset = False

        # Mouse scroll wheel handling in Inventory screen
        if screen == GameScreen.INVENTORY:
            wheel_move = pr.get_mouse_wheel_move()
            if wheel_move > 0 and inventory_scroll > 0:
                inventory_scroll -= 1
            elif wheel_move < 0 and (inventory_scroll + inventory_visible_items) < len(inventory):
                inventory_scroll += 1

        # Drawing loop
        pr.begin_drawing()
        pr.clear_background(pr.RAYWHITE)

        if screen == GameScreen.CHARACTER_SELECT:
            pr.draw_text("Select a character", 60, 20, 32, pr.DARKGRAY)
            draw_top_button(pr.Rectangle(720, 20, 180, 40), f"Inventory ({len(inventory)})")
            
            for char in characters:
                hover = pr.check_collision_point_rec(mouse_pos, char.box)
                pr.draw_rectangle_rounded(char.box, 0.16, 0, char.color)
                pr.draw_rectangle_rounded_lines(char.box, 0.16, 0, pr.BLACK if hover else pr.WHITE)
                pr.draw_text(char.name, int(char.box.x) + 20, int(char.box.y) + 65, 28, pr.WHITE)

        elif screen == GameScreen.INVENTORY:
            pr.draw_text("Inventory", 60, 22, 32, pr.DARKGRAY)
            draw_top_button(pr.Rectangle(500, 20, 180, 40), "Confirm Reset" if confirm_inventory_reset else "Reset Save")
            draw_top_button(pr.Rectangle(720, 20, 180, 40), "Back")

            if confirm_inventory_reset:
                pr.draw_text("Click Confirm Reset to erase saved evidence.", 80, 490, 20, pr.MAROON)

            if not inventory:
                pr.draw_text("No evidence collected yet.", 80, 100, 24, pr.DARKGRAY)
            else:
                inv_list = sorted(list(inventory))
                visible_count = min(len(inv_list) - inventory_scroll, inventory_visible_items)

                for i in range(visible_count):
                    raw_id = inv_list[inventory_scroll + i]
                    display_name = raw_id.replace("_", " ")
                    pr.draw_text(f"- {display_name}", 80, 90 + i * 34, 22, pr.DARKGRAY)

                if len(inv_list) > inventory_visible_items:
                    scroll_info = f"{inventory_scroll + 1}-{inventory_scroll + visible_count} of {len(inv_list)} (scroll to view more)"
                    pr.draw_text(scroll_info, 80, 510, 18, pr.GRAY)

        elif screen == GameScreen.DIALOGUE:
            char_name = characters[active_character].name if active_character >= 0 else ""
            pr.draw_text(f"Character: {char_name}", 60, 22, 28, pr.DARKGRAY)
            draw_top_button(pr.Rectangle(520, 20, 180, 40), f"Inventory ({len(inventory)})")
            draw_top_button(pr.Rectangle(720, 20, 180, 40), "Back")

            if not state.ended and state.current_node_index >= 0:
                current_node = nodes[state.current_node_index]
                pr.draw_rectangle_rounded(pr.Rectangle(60, 80, 840, 170), 0.18, 0, pr.LIGHTGRAY)
                draw_wrapped_text(current_node.spoken_text, 90, 100, 22, 780, 145, pr.BLACK)

                for i, choice_text in enumerate(current_node.choices):
                    box = pr.Rectangle(80, 260 + i * 90, 800, 54)
                    hover = pr.check_collision_point_rec(mouse_pos, box)
                    label = f"{i + 1}. {choice_text}"
                    draw_choice_button(box, label, hover)
            else:
                pr.draw_rectangle_rounded(pr.Rectangle(60, 120, 840, 200), 0.18, 0, pr.LIGHTGRAY)
                pr.draw_text("Dialogue complete.", 120, 175, 32, pr.DARKGRAY)
                pr.draw_text("Click Back to choose another character.", 120, 220, 22, pr.DARKGRAY)

        pr.end_drawing()

    pr.close_window()


if __name__ == "__main__":
    main()




