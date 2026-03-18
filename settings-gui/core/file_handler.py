# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""
File handler for reading and writing Fcitx5 array-based INI configurations
like Macro and Custom Keymap. Implements a custom parser to avoid configparser's
strict section header requirements.
"""

from pathlib import Path


class Fcitx5ConfigHandler:
    """Parses and writes Fcitx5 specific INI formats (e.g., [Macro/0])."""

    def __init__(self):
        self.config_dir = Path.home() / ".config" / "fcitx5" / "conf"
        self.config_dir.mkdir(parents=True, exist_ok=True)

        self.macro_file = self.config_dir / "lotus-macro-table.conf"
        self.keymap_file = self.config_dir / "lotus-custom-keymap.conf"

    def read_array_config(self, filepath: Path, prefix: str) -> list:
        """
        Reads an array config from Fcitx5 INI manually.
        """
        if not filepath.exists():
            return []

        result = []
        try:
            with open(filepath, "r", encoding="utf-8") as f:
                lines = f.readlines()
        except Exception as e:
            print(f"Error reading {filepath}: {e}")
            return []

        current_item = {}
        in_target_section = False

        for line in lines:
            line = line.strip()
            # Ignore empty lines and comments
            if not line or line.startswith("#"):
                continue

            # Check for section headers like [Macro/0]
            if line.startswith("[") and line.endswith("]"):
                section_name = line[1:-1]

                # Save previous item if we are transitioning to a new section
                if in_target_section and current_item:
                    result.append(current_item)
                    current_item = {}

                # Check if this is the section prefix we want
                if section_name.startswith(f"{prefix}/"):
                    in_target_section = True
                else:
                    in_target_section = False
                continue

            # Parse Key=Value pairs if we are inside the desired section
            if in_target_section and "=" in line:
                key, val = line.split("=", 1)
                current_item[key.strip()] = val.strip()

        # Add the very last item if the file ends
        if in_target_section and current_item:
            result.append(current_item)

        return result

    def write_array_config(self, filepath: Path, prefix: str, data: list):
        """Writes a list of dicts to Fcitx5 INI array format."""
        content = ""

        if not data:
            content = f"{prefix}=\n"
        else:
            for i, item in enumerate(data):
                content += f"[{prefix}/{i}]\n"
                content += f"Key={item.get('Key', '')}\n"
                content += f"Value={item.get('Value', '')}\n\n"

        try:
            with open(filepath, "w", encoding="utf-8") as f:
                f.write(content)
        except Exception as e:
            print(f"Error writing to {filepath}: {e}")
