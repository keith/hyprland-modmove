#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/devices/IKeyboard.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/managers/CursorManager.hpp>
#include <hyprland/src/managers/cursor/CursorShapeOverrideController.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/protocols/XDGShell.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/xwayland/XSurface.hpp>

#include <stdexcept>
#include <string>

inline HANDLE PHANDLE = nullptr;

namespace {

constexpr uint32_t REQUIRED_MODS = HL_MODIFIER_CTRL | HL_MODIFIER_ALT;
constexpr uint32_t RESIZE_MOD = HL_MODIFIER_SHIFT;

CHyprSignalListener g_mouseMoveListener;
PHLWINDOWREF        g_dragWindow;
Vector2D            g_lastCursorPos;
Vector2D            g_resizeStartPos;
Vector2D            g_resizeStartSize;
bool                g_dragging = false;
bool                g_resizing = false;
bool                g_cursorOverridden = false;

enum class eDragMode {
    MOVE,
    RESIZE,
};

bool modifiersPressed() {
    if (!g_pInputManager)
        return false;

    return (g_pInputManager->getModsFromAllKBs() & REQUIRED_MODS) == REQUIRED_MODS;
}

uint32_t currentMods() {
    if (!g_pInputManager)
        return 0;

    return g_pInputManager->getModsFromAllKBs();
}

eDragMode currentMode() {
    return (currentMods() & RESIZE_MOD) ? eDragMode::RESIZE : eDragMode::MOVE;
}

void setCursorForMode(eDragMode mode) {
    if (!Cursor::overrideController)
        return;

    const auto SHAPE = mode == eDragMode::RESIZE ? "se-resize" : "grab";
    Cursor::overrideController->setOverride(SHAPE, Cursor::CURSOR_OVERRIDE_SPECIAL_ACTION);
    if (g_pCursorManager)
        g_pCursorManager->setCursorFromName(SHAPE);
    if (g_pHyprRenderer)
        g_pHyprRenderer->setCursorFromName(SHAPE, true);
    g_cursorOverridden = true;
}

void clearCursorOverride() {
    if (!g_cursorOverridden || !Cursor::overrideController)
        return;

    Cursor::overrideController->unsetOverride(Cursor::CURSOR_OVERRIDE_SPECIAL_ACTION);
    g_cursorOverridden = false;
}

void stopDrag() {
    g_dragging = false;
    g_resizing = false;
    g_dragWindow.reset();
    g_lastCursorPos = {};
    g_resizeStartPos = {};
    g_resizeStartSize = {};
    clearCursorOverride();
}

void startDragIfNeeded(const Vector2D& cursorPos, eDragMode mode) {
    if (!g_pCompositor || !g_layoutManager)
        return;

    if (g_dragging && g_resizing == (mode == eDragMode::RESIZE)) {
        setCursorForMode(mode);
        return;
    }

    const auto WINDOW = g_pCompositor->vectorToWindowUnified(
        cursorPos,
        Desktop::View::RESERVED_EXTENTS | Desktop::View::INPUT_EXTENTS | Desktop::View::ALLOW_FLOATING);
    if (!WINDOW || !WINDOW->m_isFloating || !WINDOW->m_target)
        return;

    g_dragWindow = WINDOW;
    g_lastCursorPos = cursorPos;
    g_resizeStartPos = WINDOW->m_position;
    g_resizeStartSize = WINDOW->m_size;
    g_dragging = true;
    g_resizing = mode == eDragMode::RESIZE;

    g_pCompositor->changeWindowZOrder(WINDOW, true);
    g_layoutManager->bringTargetToTop(WINDOW->m_target);
    setCursorForMode(mode);
}

void resizeWindow(PHLWINDOW window, const Vector2D& delta) {
    constexpr double MIN_SIZE = 80.0;

    const auto size = Vector2D{
        std::max(MIN_SIZE, window->m_size.x + delta.x),
        std::max(MIN_SIZE, window->m_size.y + delta.y),
    };
    const auto box = CBox{window->m_position, size};

    window->m_size = size;
    window->m_target->setPositionGlobal(box);
    window->m_target->warpPositionSize();

    if (const auto xwayland = window->m_xwaylandSurface.lock())
        xwayland->configure(box);
    else if (const auto xdg = window->m_xdgSurface.lock()) {
        if (const auto toplevel = xdg->m_toplevel.lock()) {
            const auto serial = toplevel->setSize(size);
            window->m_pendingSizeAck = {{serial, size}};
            window->m_pendingSizeAcks.emplace_back(serial, size);
        }
    }

    window->m_pendingReportedSize = size;
    window->m_target->damageEntire();
    if (g_pHyprRenderer)
        g_pHyprRenderer->damageWindow(window, true);
}

void handleMouseMove(const Vector2D& cursorPos, Event::SCallbackInfo& callbackInfo) {
    if (!modifiersPressed()) {
        stopDrag();
        return;
    }

    const auto MODE = currentMode();
    startDragIfNeeded(cursorPos, MODE);

    const auto WINDOW = g_dragWindow.lock();
    if (!g_dragging || !WINDOW || !WINDOW->m_target) {
        stopDrag();
        return;
    }

    const auto DELTA = cursorPos - g_lastCursorPos;
    if (DELTA == Vector2D{})
        return;

    if (MODE == eDragMode::RESIZE)
        resizeWindow(WINDOW, DELTA);
    else
        g_layoutManager->moveTarget(DELTA, WINDOW->m_target);

    g_lastCursorPos = cursorPos;
    setCursorForMode(MODE);

    callbackInfo.cancelled = true;
}

} // namespace

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string COMPOSITOR_HASH = __hyprland_api_get_hash();
    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();

    if (COMPOSITOR_HASH != CLIENT_HASH) {
        HyprlandAPI::addNotification(PHANDLE, "[modmove] Mismatched Hyprland headers. Refusing to load.", CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error("[modmove] Version mismatch");
    }

    g_mouseMoveListener = Event::bus()->m_events.input.mouse.move.listen(handleMouseMove);

    return {"modmove", "Move floating windows by holding CTRL+ALT and hovering.", "ksmiley", "0.1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    stopDrag();
    g_mouseMoveListener.reset();
}
