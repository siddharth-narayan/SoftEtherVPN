{ 
  inputs = {
    openssl.url = "github:siddharth-narayan/openssl-with-providers";
  };

  outputs = { self, nixpkgs, openssl, ... }:
  let pkgs = import nixpkgs { system = "x86_64-linux"; };
  in
  {
    packages.x86_64-linux.default = self.packages.x86_64-linux.openssl_with_oqsprovider;

    packages.x86_64-linux.openssl_with_oqsprovider = pkgs.stdenv.mkDerivation {

      pname = "softether";
      version = "5.02.5183";
      src = /home/siddharth/projects/github/SoftEtherVPN-fork;
      dontFixCmake = true;
      buildInputs = with pkgs; [ 
        libsodium 
        pkgconf 
        tree 
        cmake
        openssl.outputs.packages.x86_64-linux.default
        openssl.outputs.packages.x86_64-linux.oqsprovider-static
        liboqs
        readline
        ncurses 
        zlib 
      ];

      preConfigure = ''
          mkdir -p $out/
          # CMAKE_INSTALL_LIBEXECDIR="libexec" CMAKE_FLAGS="-DCMAKE_INSTALL_LIBEXECDIR=libexec -DSE_PIDDIR=/tmp/softether -DSE_LOGDIR=/tmp/log/softether -DSE_DBDIR=/tmp/lib/softether" ./configure
          CMAKE_INSTALL_LIBEXECDIR="libexec" CMAKE_FLAGS="-DCMAKE_INSTALL_LIBEXECDIR=libexec -DCMAKE_BUILD_TYPE=Debug -DSE_PIDDIR=/tmp/softether -DSE_LOGDIR=/tmp/log/softether -DSE_DBDIR=/tmp/lib/softether" ./configure
      '';

      meta = with pkgs.lib; {
          description = "An Open-Source Free Cross-platform Multi-protocol VPN Program";
          homepage = "https://www.softether.org/";
          license = licenses.asl20;
          maintainers = [];
          platforms = [ "x86_64-linux" ];
      };
    };
    
    packages.x86_64-linux.openssl_default = pkgs.stdenv.mkDerivation {

      pname = "softether";
      version = "5.02.5183";
      src = /home/siddharth/projects/github/SoftEtherVPN-fork;
      dontFixCmake = true;
      buildInputs = with pkgs; [ 
        libsodium 
        pkgconf 
        tree 
        cmake
        pkgs.openssl
        readline
        ncurses 
        zlib 
      ];

      preConfigure = ''
          mkdir -p $out/
          # CMAKE_INSTALL_LIBEXECDIR="libexec" CMAKE_FLAGS="-DCMAKE_INSTALL_LIBEXECDIR=libexec -DSE_PIDDIR=/tmp/softether -DSE_LOGDIR=/tmp/log/softether -DSE_DBDIR=/tmp/lib/softether" ./configure
          CMAKE_INSTALL_LIBEXECDIR="libexec" CMAKE_FLAGS="-DCMAKE_INSTALL_LIBEXECDIR=libexec -DCMAKE_BUILD_TYPE=Debug -DSE_PIDDIR=/tmp/softether -DSE_LOGDIR=/tmp/log/softether -DSE_DBDIR=/tmp/lib/softether" ./configure
      '';

      meta = with pkgs.lib; {
          description = "An Open-Source Free Cross-platform Multi-protocol VPN Program";
          homepage = "https://www.softether.org/";
          license = licenses.asl20;
          maintainers = [];
          platforms = [ "x86_64-linux" ];
      };
    };

    packages.x86_64-linux.openssl_1_1 = pkgs.stdenv.mkDerivation {

      pname = "softether";
      version = "5.02.5183";
      src = /home/siddharth/projects/github/SoftEtherVPN-fork;
      dontFixCmake = true;
      buildInputs = with pkgs; [ 
        libsodium 
        pkgconf 
        tree 
        cmake
        openssl_1_1
        openssl.outputs.packages.x86_64-linux.oqsprovider-static
        liboqs
        readline
        ncurses 
        zlib 
      ];

      preConfigure = ''
          mkdir -p $out/
          # CMAKE_INSTALL_LIBEXECDIR="libexec" CMAKE_FLAGS="-DCMAKE_INSTALL_LIBEXECDIR=libexec -DSE_PIDDIR=/tmp/softether -DSE_LOGDIR=/tmp/log/softether -DSE_DBDIR=/tmp/lib/softether" ./configure
          CMAKE_INSTALL_LIBEXECDIR="libexec" CMAKE_FLAGS="-DCMAKE_INSTALL_LIBEXECDIR=libexec -DCMAKE_BUILD_TYPE=Debug -DSE_PIDDIR=/tmp/softether -DSE_LOGDIR=/tmp/log/softether -DSE_DBDIR=/tmp/lib/softether" ./configure
      '';

      meta = with pkgs.lib; {
          description = "An Open-Source Free Cross-platform Multi-protocol VPN Program";
          homepage = "https://www.softether.org/";
          license = licenses.asl20;
          maintainers = [];
          platforms = [ "x86_64-linux" ];
      };
    };

    devShells.x86_64-linux.default = pkgs.mkShell {
      packages = with pkgs; [
        libgcc
        libcxx
        cmake
        gnumake
        pkgs.openssl
        libiconv
        readline
        ncurses
        pkg-config
        libsodium
        zlib
        # Change this to change what openssl version is built
        self.packages.x86_64-linux.openssl_default
      ];
    };
  };
}
