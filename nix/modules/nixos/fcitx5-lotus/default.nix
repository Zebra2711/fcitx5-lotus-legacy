{
  config,
  lib,
  inputs,
  pkgs,
  ...
}:
with lib; let
  cfg = config.services.fcitx5-lotus;
  fcitx5-lotus = inputs.self.packages.${pkgs.stdenv.hostPlatform.system}.fcitx5-lotus;
in {
  options.services.fcitx5-lotus = {
    enable = mkEnableOption "Fcitx5 Lotus Server";
    user = mkOption {
      type = types.str;
      default = "";
      description = "User name of the Server";
    };
  };

  config = mkIf cfg.enable {
    i18n.inputMethod.fcitx5.addons = [fcitx5-lotus];

    users.users.uinput_proxy = {
      isSystemUser = true;
      group = "input";
    };

    services.udev.packages = [fcitx5-lotus];
    systemd.packages = [fcitx5-lotus];

    systemd.targets.multi-user.wants = ["fcitx5-lotus-server@${cfg.user}.service"];
  };
}
