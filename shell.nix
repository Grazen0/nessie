{
  pkgs ? import <nixpkgs> { },
}:
pkgs.mkShell {
  packages = with pkgs; [
    fceux
    pkg-config
    raylib
  ];

  hardeningDisable = [ "fortify" ];

}
