{
  description = "A NES emulator written in C.";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
    flake-parts.url = "github:hercules-ci/flake-parts";
    systems.url = "github:nix-systems/default";
  };

  outputs =
    inputs@{
      self,
      flake-parts,
      ...
    }:
    flake-parts.lib.mkFlake { inherit inputs; } {
      systems = import inputs.systems;

      perSystem =
        {
          self',
          pkgs,
          system,
          ...
        }:
        let
          inherit (pkgs) lib;
        in
        {
          packages =
            let
              mkNessie =
                { frontend }:
                pkgs.stdenv.mkDerivation {
                  pname = "nessie";
                  version = "main";

                  src = lib.cleanSource ./.;

                  nativeBuildInputs = with pkgs; [
                    meson
                    ninja
                    pkg-config
                  ];

                  buildInputs = with pkgs; (lib.optionals (frontend == "raylib") [ raylib ]);

                  mesonFlags = [
                    "-Dfrontend=${frontend}"
                  ];

                  doCheck = true;

                  meta = with lib; {
                    description = "A NES emulator written in C.";
                    homepage = "https://codeberg.org/Grazen0/nessie";
                    license = licenses.gpl3;
                  };
                };
            in
            {
              nessie = self'.packages.nessie-raylib;
              nessie-raylib = mkNessie { frontend = "raylib"; };

              default = self'.packages.nessie;
            };

          devShells.default = pkgs.mkShell {
            inputsFrom = lib.attrValues self'.packages;
            packages = with pkgs; [
              clang-tools
              fceux
            ];
            hardeningDisable = [ "fortify" ];
          };
        };
    };
}
