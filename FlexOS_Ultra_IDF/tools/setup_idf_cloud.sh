#!/usr/bin/env bash
# Prepara ESP-IDF v5.5.5 + la cadena RISC-V en un contenedor de la nube cuyo proxy solo deja pasar
# GitHub y PyPI (dl.espressif.com y components.espressif.com estan bloqueados). Idempotente; deja
# ~/esp/env.sh para "source ~/esp/env.sh && tools/build.sh --rev v3". Tarda unos minutos.
#   - IDF_GITHUB_ASSETS=github.com: las herramientas se bajan de las releases de GitHub.
#   - install-python-env --no-constraints + archivo de restricciones vacio: el fichero oficial
#     (espidf.constraints.v5.5.txt) solo vive en dl.espressif.com.
#   - lvgl y esp_littlefs se obtienen por git (main/idf_component.yml): no hace falta el registro.
set -uo pipefail
mkdir -p ~/esp && cd ~/esp
export IDF_GITHUB_ASSETS=github.com IDF_TOOLS_PATH="$HOME/.espressif"
if [ ! -d esp-idf/.git ]; then
    git clone --depth 1 -b v5.5.5 --recursive --shallow-submodules https://github.com/espressif/esp-idf.git esp-idf || exit 10
fi
cd esp-idf
./install.sh esp32p4 >/dev/null 2>&1 || true            # herramientas (fallara solo en el entorno Python)
python3 tools/idf_tools.py install-python-env --no-constraints || exit 11
: > "$IDF_TOOLS_PATH/espidf.constraints.v5.5.txt"
cat > ~/esp/env.sh <<'ENV'
export IDF_GITHUB_ASSETS=github.com
export IDF_TOOLS_PATH=$HOME/.espressif
. $HOME/esp/esp-idf/export.sh >/dev/null 2>&1
ENV
. ~/esp/env.sh && idf.py --version | tail -1
