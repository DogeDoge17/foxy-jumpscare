{
  description = "super scary foxy rawr";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = {
    self,
    nixpkgs,
  }: let
    forAllSystems = nixpkgs.lib.genAttrs [
      "x86_64-linux"
      "aarch64-linux"
    ];
  in {
    packages = forAllSystems (
      system: let
        pkgs = import nixpkgs {inherit system;};
      in {
        default = pkgs.stdenv.mkDerivation {
          pname = "foxy-jumpscare";
          version = "0.1.0";
          src = pkgs.lib.fileset.toSource {
            root = ./.;
            fileset = pkgs.lib.fileset.unions [
              ./CMakeLists.txt
              ./main.c
            ];
          };
          strictDeps = true;
          nativeBuildInputs = with pkgs; [
            cmake
            ninja
            pkg-config
            wayland-scanner
          ];
          buildInputs = with pkgs; [
            wayland-protocols
            wlr-protocols
            wayland
            libffi
            libpng
            zlib
            miniaudio
            libpulseaudio
            alsa-lib
            sndio
            libjack2
          ];
          cmakeBuildType = "MinSizeRel";
          meta = {
            description = "Small Wayland layer-shell client";
            mainProgram = "foxy-jumpscare";
            platforms = pkgs.lib.platforms.linux;
          };
        };
      }
    );

    apps = forAllSystems (system: {
      default = {
        type = "app";
        program = "${self.packages.${system}.default}/bin/foxy-jumpscare";
        inherit (self.packages.${system}.default) meta;
      };
    });

    devShells = forAllSystems (
      system: let
        pkgs = import nixpkgs {inherit system;};
      in {
        default = pkgs.mkShell {
          inputsFrom = [self.packages.${system}.default];
          packages = [pkgs.clang-tools];
        };
      }
    );
  };
}
