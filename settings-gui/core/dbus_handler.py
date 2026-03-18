# SPDX-FileCopyrightText: 2026 Nguyen Hoang Ky <nhktmdzhg@gmail.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""
D-Bus handler to communicate with Fcitx5 Controller.
"""

import dbus


class LotusDBusHandler:
    def __init__(self):
        self.addon_name = "fcitx://config/addon/lotus"
        try:
            self.bus = dbus.SessionBus()
            self.proxy = self.bus.get_object("org.fcitx.Fcitx5", "/controller")
            self.iface = dbus.Interface(self.proxy, "org.fcitx.Fcitx.Controller1")
        except dbus.DBusException as e:
            print(f"Fcitx5 D-Bus Error: {e}")
            self.iface = None

    def get_config(self) -> dict:
        """Get config from Fcitx5 and convert to Python dict/list."""
        if not self.iface:
            return {}
        try:
            values, metadata = self.iface.GetConfig(self.addon_name)
            return {
                "values": self._clean_dbus(values),
                "metadata": self._clean_dbus(metadata),
            }
        except Exception as e:
            print(f"Failed to fetch config: {e}")
            return {}

    def set_config(self, values_dict: dict):
        """Set config and send to Fcitx5."""
        if not self.iface:
            return
        try:
            dbus_dict = self._prepare_dbus_data(values_dict)
            self.iface.SetConfig(self.addon_name, dbus_dict)
        except Exception as e:
            print(f"Failed to set config: {e}")

    def _prepare_dbus_data(self, data):
        """Prepare data to be sent to Fcitx5 in dbus types with signatures."""
        if isinstance(data, dict):
            # Fcitx5 expects dicts to be a{sv} (String to Variant)
            formatted = {str(k): self._prepare_dbus_data(v) for k, v in data.items()}
            return dbus.Dictionary(formatted, signature="sv")
        elif isinstance(data, list):
            # Arrays must be Array of Variants (av)
            formatted = [self._prepare_dbus_data(v) for v in data]
            return dbus.Array(formatted, signature="v")
        elif isinstance(data, bool):
            return dbus.Boolean(data)
        return data

    def _clean_dbus(self, data):
        """Convert dbus types to Python types."""
        if isinstance(data, dbus.Dictionary):
            return {str(k): self._clean_dbus(v) for k, v in data.items()}
        elif isinstance(data, (dbus.Array, dbus.Struct, list, tuple)):
            return [self._clean_dbus(v) for v in data]
        elif isinstance(data, dbus.Boolean):
            return bool(data)
        elif isinstance(
            data,
            (dbus.Int16, dbus.Int32, dbus.Int64, dbus.UInt16, dbus.UInt32, dbus.UInt64),
        ):
            return int(data)
        elif isinstance(data, dbus.Double):
            return float(data)
        elif isinstance(data, dbus.String):
            return str(data)
        return data
