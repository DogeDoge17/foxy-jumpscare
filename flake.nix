{
  description = "super scary foxy rawr";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  inputs.nix-appimage.url = "github:ralismark/nix-appimage";

  outputs = {
    self,
    nixpkgs,
    nix-appimage,
  }: let
    forAllSystems = nixpkgs.lib.genAttrs [
      "x86_64-linux"
      "aarch64-linux"
    ];
  in {
    packages = forAllSystems (
      system: let
        pkgs = import nixpkgs {inherit system;};
        smallStatic = package: package.overrideAttrs (old: {
          dontDisableStatic = true;
          env = (old.env or {}) // {
            NIX_CFLAGS_COMPILE = (old.env.NIX_CFLAGS_COMPILE or "")
              + " -Oz -flto=auto -ffat-lto-objects -ffunction-sections -fdata-sections"
              + " -fno-unwind-tables -fno-asynchronous-unwind-tables -fomit-frame-pointer";
          };
        });
        staticZlib = smallStatic (pkgs.zlib.override {
          shared = false;
          stdenv = pkgs.stdenv;
        });
        # Keep upstream flags: LTO makes libffi's callback compilation tests time out.
        staticFfi = pkgs.libffi.overrideAttrs (old: {
          dontDisableStatic = true;
          configureFlags = (old.configureFlags or []) ++ ["--disable-shared" "--enable-static"];
        });
        staticPng = smallStatic ((pkgs.libpng.override {zlib = staticZlib;}).overrideAttrs (old: {
          configureFlags = (old.configureFlags or []) ++ ["--disable-shared" "--enable-static"];
        }));
        staticWayland = smallStatic ((pkgs.wayland.override {
          libffi = staticFfi;
          withDocumentation = false;
        }).overrideAttrs (old: {
          mesonFlags = old.mesonFlags ++ ["-Ddefault_library=static"];
          separateDebugInfo = false;
        }));
        minimalSndfile = pkgs.libsndfile.overrideAttrs (old: {
          buildInputs = [];
          configureFlags = (old.configureFlags or []) ++ [
            "--disable-external-libs"
            "--disable-mpeg"
            "--disable-alsa"
          ];
          env = (old.env or {}) // {NIX_CFLAGS_LINK = "";};
        });
        desktopPulse = pkgs.libpulseaudio.override {libsndfile = minimalSndfile;};
        staticMiniaudio = smallStatic ((pkgs.miniaudio.override {
          libpulseaudio = desktopPulse;
          jackSupport = false;
        }).overrideAttrs (old: {
          buildInputs = [desktopPulse pkgs.alsa-lib pkgs.sndio];
          env = (old.env or {}) // {
            NIX_CFLAGS_COMPILE = (old.env.NIX_CFLAGS_COMPILE or "")
              + " -DNDEBUG -DMA_DEFAULT_PREMIX_STACK_SIZE_PER_CHANNEL=16384";
          };
          cmakeBuildType = "MinSizeRel";
          cmakeFlags = old.cmakeFlags ++ [
            "-DBUILD_SHARED_LIBS=OFF"
            "-DMINIAUDIO_BUILD_TESTS=OFF"
            "-DMINIAUDIO_BUILD_EXAMPLES=OFF"
            "-DMINIAUDIO_NO_LIBVORBIS=ON"
            "-DMINIAUDIO_NO_LIBOPUS=ON"
            "-DMINIAUDIO_NO_EXTRA_NODES=ON"
            "-DMINIAUDIO_NO_ENCODING=ON"
            "-DMINIAUDIO_NO_WAV=ON"
            "-DMINIAUDIO_NO_FLAC=ON"
            "-DMINIAUDIO_NO_GENERATION=ON"
            "-DCMAKE_C_FLAGS_MINSIZEREL=-Oz"
          ];
          doCheck = false; # Upstream tests need the disabled codecs and audio hardware.
        }));
      in {
        appimage = nix-appimage.lib.${system}.mkAppImage {
          program = "${self.packages.${system}.default}/bin/foxy-jumpscare";
          # Upstream's fallback mounts nix/ onto nix/store, adding an extra directory.
          apprun = pkgs.pkgsStatic.runCommandCC "AppRun" {} ''
            mkdir -p "$out/mountroot"
            cp ${nix-appimage}/appruns/userns-chroot/main.c main.c
            substituteInPlace main.c --replace-fail \
              'strprintf("%s/nix", appdir)' 'strprintf("%s/nix/store", appdir)'
            $CC main.c -o "$out/AppRun"
          '';
        };
        default = pkgs.stdenv.mkDerivation {
          pname = "foxy-jumpscare";
          version = "0.1.0";
          src = pkgs.lib.fileset.toSource {
            root = ./.;
            fileset = pkgs.lib.fileset.unions [
              ./CMakeLists.txt
              ./main.c
              ./spritesheet.png
              ./jumpscare.mp3
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
            staticWayland
            staticFfi
            staticPng
            staticZlib
            staticMiniaudio
            desktopPulse
            alsa-lib
            sndio
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
