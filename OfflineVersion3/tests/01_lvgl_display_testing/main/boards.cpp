#include "boards.h"

using namespace lgfx::v1;

#ifdef CONFIG_IDF_TARGET_ESP32S3
Panel_Device* panel_load_from_bc02(board_pins_t* pins);
#endif

PanelLan::PanelLan(panelLan_board_t board) {
  _board = board;
  setPanel(nullptr);
}

bool PanelLan::init_impl(bool use_reset, bool use_clear) {
  Panel_Device* panel = nullptr;
  
  // Strictly load the BC02 board. Ignored the missing SC01/SC05 files.
  if (_board == BOARD_BC02) {
      panel = panel_load_from_bc02(&pins);
  }

  if (panel == nullptr) {
    assert(0);
  }

  setPanel(panel);
  return LGFX_Device::init_impl(false, use_clear);
}