#!/usr/bin/env bash
# ==============================================================================
#  GrassGreen 4.0 — Universal Linux Installer
#  Поддержка всех дистрибутивов Linux (Arch Linux, Ubuntu, Debian, Fedora и др.)
# ==============================================================================

set -e

# Цветовая палитра терминала
GREEN='\033[0;32m'
BRIGHT_GREEN='\033[1;32m'
CYAN='\033[0;36m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
BOLD='\033[1m'
NC='\033[0m' # No Color

echo -e "${BRIGHT_GREEN}"
cat << "EOF"
   ____                                _____                           
  / ___|_ __ __ _ ___ ___             / ____|_ __ ___  ___ _ __  
 | |  _| '__/ _` / __/ __|  ______   | |  __| '__/ _ \/ _ \ '_ \ 
 | |_| | | | (_| \__ \__ \ |______|  | |__|_| | |  __/  __/ | | |
  \____|_|  \__,_|___/___/            \_____|_|  \___|\___|_| |_|
EOF
echo -e "${NC}"
echo -e "${BOLD}GrassGreen (G&G) Language — Universal Linux Installer v4.0${NC}"
echo -e "Target OS: Linux (optimized for Arch Linux & all major distros)\n"

# ------------------------------------------------------------------------------
# 1. Определение дистрибутива Linux и пакетного менеджера
# ------------------------------------------------------------------------------
DISTRO="unknown"
PKG_MGR="unknown"

if [ -f /etc/os-release ]; then
    . /etc/os-release
    DISTRO=$ID
    DISTRO_LIKE=${ID_LIKE:-""}
elif [ -f /etc/arch-release ]; then
    DISTRO="arch"
elif [ -f /etc/debian_version ]; then
    DISTRO="debian"
elif [ -f /etc/fedora-release ]; then
    DISTRO="fedora"
fi

case "$DISTRO" in
    (arch|manjaro|endeavouros|garuda|artix)
        PKG_MGR="pacman"
        ;;
    (ubuntu|debian|linuxmint|pop|kali|elementary)
        PKG_MGR="apt"
        ;;
    (fedora|rhel|centos|rocky|almalinux)
        PKG_MGR="dnf"
        ;;
    (opensuse*|sles)
        PKG_MGR="zypper"
        ;;
    (alpine)
        PKG_MGR="apk"
        ;;
    (void)
        PKG_MGR="xbps"
        ;;
    (gentoo)
        PKG_MGR="emerge"
        ;;
    (*)
        if [[ "$DISTRO_LIKE" == *"arch"* ]]; then
            PKG_MGR="pacman"
        elif [[ "$DISTRO_LIKE" == *"debian"* ]] || [[ "$DISTRO_LIKE" == *"ubuntu"* ]]; then
            PKG_MGR="apt"
        elif [[ "$DISTRO_LIKE" == *"fedora"* ]] || [[ "$DISTRO_LIKE" == *"rhel"* ]]; then
            PKG_MGR="dnf"
        fi
        ;;
esac

echo -e "${CYAN}[*] Detected Distribution:${NC} ${BOLD}${DISTRO}${NC} (Package Manager: ${BOLD}${PKG_MGR}${NC})"

# Проверка прав суперпользователя
SUDO=""
if [ "$EUID" -ne 0 ]; then
    if command -v sudo >/dev/null 2>&1; then
        SUDO="sudo"
    elif command -v doas >/dev/null 2>&1; then
        SUDO="doas"
    fi
fi

# ------------------------------------------------------------------------------
# 2. Установка зависимостей сборки (GCC C++20, make, git)
# ------------------------------------------------------------------------------
echo -e "${CYAN}[*] Checking build dependencies (g++, git, make)...${NC}"

NEED_DEPS=0
if ! command -v g++ >/dev/null 2>&1; then NEED_DEPS=1; fi
if ! command -v git >/dev/null 2>&1; then NEED_DEPS=1; fi
if ! command -v make >/dev/null 2>&1; then NEED_DEPS=1; fi

if [ $NEED_DEPS -eq 1 ]; then
    echo -e "${YELLOW}[!] Missing build tools. Installing via ${PKG_MGR}...${NC}"
    case "$PKG_MGR" in
        (pacman)
            echo -e "${GREEN}[pacman] Installing base-devel, gcc, git...${NC}"
            $SUDO pacman -S --needed --noconfirm base-devel gcc git
            ;;
        (apt)
            echo -e "${GREEN}[apt] Installing build-essential, g++, git...${NC}"
            $SUDO apt-get update -qq
            $SUDO apt-get install -y build-essential gcc g++ git curl
            ;;
        (dnf)
            echo -e "${GREEN}[dnf] Installing development-tools, gcc-c++, git...${NC}"
            $SUDO dnf install -y gcc-c++ git make curl
            ;;
        (zypper)
            echo -e "${GREEN}[zypper] Installing gcc-c++, git, make...${NC}"
            $SUDO zypper install -y gcc-c++ git make curl
            ;;
        (apk)
            echo -e "${GREEN}[apk] Installing build-base, gcc, g++, git...${NC}"
            $SUDO apk add --no-cache build-base gcc g++ git curl bash
            ;;
        (xbps)
            echo -e "${GREEN}[xbps] Installing base-devel, gcc, git...${NC}"
            $SUDO xbps-install -Sy base-devel gcc git curl
            ;;
        (emerge)
            echo -e "${GREEN}[emerge] Installing sys-devel/gcc, dev-vcs/git...${NC}"
            $SUDO emerge --ask n sys-devel/gcc dev-vcs/git
            ;;
        (*)
            echo -e "${RED}[ERROR] Unknown package manager. Please ensure g++ (supporting C++20), git and make are installed.${NC}"
            ;;
    esac
else
    echo -e "${GREEN}[OK] All core dependencies are installed.${NC}"
fi

# Проверка поддержки C++20 компилятором
GCC_VERSION=$(g++ -dumpversion | cut -d. -f1)
if [ "$GCC_VERSION" -lt 10 ]; then
    echo -e "${YELLOW}[WARNING] GCC version is $GCC_VERSION. GrassGreen 4.0 requires C++20 (GCC 10 or higher recommended).${NC}"
fi

# ------------------------------------------------------------------------------
# 3. Подготовка исходного кода GrassGreen
# ------------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK_DIR="$SCRIPT_DIR"

if [ ! -f "$WORK_DIR/src/grassc.cpp" ]; then
    echo -e "${CYAN}[*] Repository files not found in current directory. Downloading GrassGreen...${NC}"
    TMP_DIR="/tmp/grassgreen-build-$$"
    git clone --depth 1 https://github.com/m1oky/GrassGreen.git "$TMP_DIR"
    WORK_DIR="$TMP_DIR"
fi

cd "$WORK_DIR"
mkdir -p bin

# ------------------------------------------------------------------------------
# 4. Компиляция компилятора GrassGreen v4.0
# ------------------------------------------------------------------------------
echo -e "${CYAN}[*] Compiling GrassGreen v4.0 (Single Compilation Unit / C++20 -O3)...${NC}"

g++ -std=c++20 -O3 -s \
  src/grassc.cpp \
  src/GrassHAL.cpp \
  src/GrassMMIO.cpp \
  src/GrassThread.cpp \
  src/GrassJIT.cpp \
  src/GrassImport.cpp \
  src/GrassOptimizer.cpp \
  src/GrassSyntax.cpp \
  src/GrassProc.cpp \
  src/GrassFsys.cpp \
  src/GrassTime.cpp \
  src/GrassNet.cpp \
  src/GrassJson.cpp \
  src/GrassSys.cpp \
  src/gui_engine.cpp \
  src/GrassWeb.cpp \
  src/GSE.cpp \
  src/FastMath.cpp \
  src/CoreMath.cpp \
  src/GrassMemory.cpp \
  src/GrassRandom.cpp \
  src/GrassAudio.cpp \
  src/GrassTg.cpp \
  src/Grass2DGraphics.cpp \
  -o bin/grassc \
  -pthread

cp bin/grassc bin/grg
chmod +x bin/grassc bin/grg

echo -e "${GREEN}[OK] Build successful: bin/grassc & bin/grg${NC}"

# ------------------------------------------------------------------------------
# 5. Установка в систему (/usr/local/bin или ~/.local/bin)
# ------------------------------------------------------------------------------
INSTALL_DIR="/usr/local/bin"
SHARE_DIR="/usr/local/share/grassgreen"
USE_LOCAL=0

if [ "$EUID" -ne 0 ] && [ -z "$SUDO" ]; then
    INSTALL_DIR="$HOME/.local/bin"
    SHARE_DIR="$HOME/.local/share/grassgreen"
    USE_LOCAL=1
fi

echo -e "${CYAN}[*] Installing binaries to ${INSTALL_DIR}...${NC}"

if [ $USE_LOCAL -eq 1 ]; then
    mkdir -p "$INSTALL_DIR"
    cp bin/grassc "$INSTALL_DIR/grassc"
    cp bin/grg "$INSTALL_DIR/grg"
    chmod +x "$INSTALL_DIR/grassc" "$INSTALL_DIR/grg"

    mkdir -p "$SHARE_DIR"
    cp -r docs examples "$SHARE_DIR/" 2>/dev/null || true
else
    $SUDO mkdir -p "$INSTALL_DIR"
    $SUDO cp bin/grassc "$INSTALL_DIR/grassc"
    $SUDO cp bin/grg "$INSTALL_DIR/grg"
    $SUDO chmod +x "$INSTALL_DIR/grassc" "$INSTALL_DIR/grg"

    $SUDO mkdir -p "$SHARE_DIR"
    $SUDO cp -r docs examples "$SHARE_DIR/" 2>/dev/null || true
fi

# ------------------------------------------------------------------------------
# 6. Проверка и настройка переменной окружения PATH
# ------------------------------------------------------------------------------
if [[ ":$PATH:" != *":$INSTALL_DIR:"* ]]; then
    echo -e "${YELLOW}[!] $INSTALL_DIR is not in your current PATH.${NC}"
    for RC in "$HOME/.bashrc" "$HOME/.zshrc" "$HOME/.profile"; do
        if [ -f "$RC" ]; then
            if ! grep -q "PATH=.*$INSTALL_DIR" "$RC"; then
                echo "export PATH=\"$INSTALL_DIR:\$PATH\"" >> "$RC"
                echo -e "${CYAN}[*] Added $INSTALL_DIR to $RC${NC}"
            fi
        fi
    done
    export PATH="$INSTALL_DIR:$PATH"
fi

# ------------------------------------------------------------------------------
# 7. Интеграция с VS Code / VSCodium (расширение синтаксиса .grass)
# ------------------------------------------------------------------------------
echo -e "${CYAN}[*] Setting up VS Code / VSCodium GrassGreen extensions...${NC}"
"$INSTALL_DIR/grassc" --install >/dev/null 2>&1 || true

# ------------------------------------------------------------------------------
# 8. Верификация установки
# ------------------------------------------------------------------------------
echo -e "${CYAN}[*] Verifying GrassGreen installation...${NC}"

TEST_DIR="/tmp/grass_test_$$"
mkdir -p "$TEST_DIR"
cat << 'EOF' > "$TEST_DIR/verify.grass"
!GrassGreen!
int main() {
    gout << "GrassGreen v4.0 is running perfectly on Linux!" << endl;
    return 0;
}
EOF

if "$INSTALL_DIR/grassc" "$TEST_DIR/verify.grass" >/dev/null 2>&1; then
    echo -e "${GREEN}[OK] Execution test PASSED!${NC}"
else
    echo -e "${YELLOW}[!] Binary installed. You can run scripts with: grassc <file.grass>${NC}"
fi
rm -rf "$TEST_DIR"

# ------------------------------------------------------------------------------
# 9. Завершение и инструкция
# ------------------------------------------------------------------------------
echo -e "\n${BRIGHT_GREEN}================================================================${NC}"
echo -e "${BRIGHT_GREEN}  GrassGreen 4.0 successfully installed on your Linux system!${NC}"
echo -e "${BRIGHT_GREEN}================================================================${NC}\n"
echo -e "You can now run GrassGreen from any directory using:"
echo -e "  ${BOLD}grassc script.grass${NC}              # Compile & run script"
echo -e "  ${BOLD}grassc -O3 -emit-exe script.grass${NC} # Compile to standalone executable"
echo -e "  ${BOLD}grg script.grass${NC}                 # Quick-run shortcut"
echo -e "  ${BOLD}grassc --help${NC}                    # View full compiler options"
echo -e "  ${BOLD}grassc -v${NC}                        # Show compiler version"
echo -e "\nDocumentation & examples located at: ${CYAN}${SHARE_DIR}${NC}\n"
