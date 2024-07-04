{ 
  inputs = {
    openssl-with-oqsprovider.url = "github:siddharth-narayan/openssl-with-providers";
  };

  outputs = { self, nixpkgs, openssl-with-oqsprovider, ... }:
  let 
    pkgs = import nixpkgs { system = "x86_64-linux"; };
    softether = pkgs.stdenv.mkDerivation {

      pname = "softether";
      version = "5.02.5183";
      src = /home/siddharth/projects/github/SoftEtherVPN;
      dontFixCmake = true;
      buildInputs = with pkgs; [ 
        libsodium 
        pkgconf 
        tree 
        cmake
        readline
        ncurses
        zlib
      ];

      preConfigure = ''
          mkdir -p $out/
          CMAKE_FLAGS="-DSE_PIDDIR=/tmp/softether -DSE_LOGDIR=/tmp/log/softether -DSE_DBDIR=/tmp/lib/softether" ./configure
      '';
      
      postInstall = ''
        echo fixing double slash
        if [ -f "$out/lib/pkgconfig/liboqs.pc" ]; then
          sed 's|//|/|g' $out/lib/pkgconfig/liboqs.pc > $out/lib/pkgconfig/liboqs.pc.tmp
          cp $out/lib/pkgconfig/liboqs.pc.tmp $out/lib/pkgconfig/liboqs.pc
        fi
      '';

      meta = with pkgs.lib; {
          description = "An Open-Source Free Cross-platform Multi-protocol VPN Program";
          homepage = "https://www.softether.org/";
          license = licenses.asl20;
          maintainers = [];
          platforms = [ "x86_64-linux" ];
      };
    };
  in
  {
    packages.x86_64-linux.default = softether.overrideAttrs (old: {buildInputs = old.buildInputs ++ [ pkgs.openssl ]; });
    packages.x86_64-linux.openssl1 = softether.overrideAttrs (old: {buildInputs = old.buildInputs ++ [ pkgs.openssl_1_1 ]; });
    devShells.x86_64-linux.default = pkgs.mkShell {
      packages = with pkgs; [
        libgcc
        libcxx
        cmake
        gnumake
        libiconv
        openssl
        readline
        ncurses
        pkg-config
        libsodium
        zlib
        # Change this to change what openssl version is built
        self.packages.x86_64-linux.default
      ];
    };
  };
}
