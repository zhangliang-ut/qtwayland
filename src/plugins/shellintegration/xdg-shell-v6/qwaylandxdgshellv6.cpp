/****************************************************************************
**
** Copyright (C) 2017 The Qt Company Ltd.
** Copyright (C) 2017 Eurogiciel, author: <philippe.coval@eurogiciel.fr>
** Contact: https://www.qt.io/licensing/
**
** This file is part of the config.tests of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qwaylandxdgshellv6_p.h"

#include <QtWaylandClient/private/qwaylanddisplay_p.h>
#include <QtWaylandClient/private/qwaylandwindow_p.h>
#include <QtWaylandClient/private/qwaylandinputdevice_p.h>
#include <QtWaylandClient/private/qwaylandscreen_p.h>
#include <QtWaylandClient/private/qwaylandabstractdecoration_p.h>

QT_BEGIN_NAMESPACE

namespace QtWaylandClient {

QWaylandXdgSurfaceV6::Toplevel::Toplevel(QWaylandXdgSurfaceV6 *xdgSurface)
    : QtWayland::zxdg_toplevel_v6(xdgSurface->get_toplevel())
    , m_xdgSurface(xdgSurface)
{
    requestWindowStates(xdgSurface->window()->window()->windowStates());
}

QWaylandXdgSurfaceV6::Toplevel::~Toplevel()
{
    if (m_applied.states & Qt::WindowActive) {
        QWaylandWindow *window = m_xdgSurface->window();
        window->display()->handleWindowDeactivated(window);
    }
    if (isInitialized())
        destroy();
}

void QWaylandXdgSurfaceV6::Toplevel::applyConfigure()
{
    if (!(m_applied.states & (Qt::WindowMaximized|Qt::WindowFullScreen)))
        m_normalSize = m_xdgSurface->m_window->window()->frameGeometry().size();

    if (m_pending.size.isEmpty() && !m_normalSize.isEmpty())
        m_pending.size = m_normalSize;

    if ((m_pending.states & Qt::WindowActive) && !(m_applied.states & Qt::WindowActive))
        m_xdgSurface->m_window->display()->handleWindowActivated(m_xdgSurface->m_window);

    if (!(m_pending.states & Qt::WindowActive) && (m_applied.states & Qt::WindowActive))
        m_xdgSurface->m_window->display()->handleWindowDeactivated(m_xdgSurface->m_window);

    // TODO: none of the other plugins send WindowActive either, but is it on purpose?
    Qt::WindowStates statesWithoutActive = m_pending.states & ~Qt::WindowActive;

    m_xdgSurface->m_window->handleWindowStatesChanged(statesWithoutActive);
    m_xdgSurface->m_window->resizeFromApplyConfigure(m_pending.size);
    QSize windowGeometrySize = m_xdgSurface->m_window->window()->frameGeometry().size();
    m_xdgSurface->set_window_geometry(0, 0, windowGeometrySize.width(), windowGeometrySize.height());
    m_applied = m_pending;
}

void QWaylandXdgSurfaceV6::Toplevel::zxdg_toplevel_v6_configure(int32_t width, int32_t height, wl_array *states)
{
    m_pending.size = QSize(width, height);

    auto *xdgStates = static_cast<uint32_t *>(states->data);
    size_t numStates = states->size / sizeof(uint32_t);

    m_pending.states = Qt::WindowNoState;

    for (size_t i = 0; i < numStates; i++) {
        switch (xdgStates[i]) {
        case ZXDG_TOPLEVEL_V6_STATE_ACTIVATED:
            m_pending.states |= Qt::WindowActive;
            break;
        case ZXDG_TOPLEVEL_V6_STATE_MAXIMIZED:
            m_pending.states |= Qt::WindowMaximized;
            break;
        case ZXDG_TOPLEVEL_V6_STATE_FULLSCREEN:
            m_pending.states |= Qt::WindowFullScreen;
            break;
        default:
            break;
        }
    }
}

void QWaylandXdgSurfaceV6::Toplevel::zxdg_toplevel_v6_close()
{
    m_xdgSurface->m_window->window()->close();
}

void QWaylandXdgSurfaceV6::Toplevel::requestWindowStates(Qt::WindowStates states)
{
    // Re-send what's different from the applied state
    Qt::WindowStates changedStates = m_applied.states ^ states;

    if (changedStates & Qt::WindowMaximized) {
        if (states & Qt::WindowMaximized)
            set_maximized();
        else
            unset_maximized();
    }

    if (changedStates & Qt::WindowFullScreen) {
        if (states & Qt::WindowFullScreen)
            set_fullscreen(nullptr);
        else
            unset_fullscreen();
    }

    // Minimized state is not reported by the protocol, so always send it
    if (states & Qt::WindowMinimized) {
        set_minimized();
        m_xdgSurface->window()->handleWindowStatesChanged(states & ~Qt::WindowMinimized);
    }
}

QWaylandXdgSurfaceV6::Popup::Popup(QWaylandXdgSurfaceV6 *xdgSurface, QWaylandXdgSurfaceV6 *parent,
                                   QtWayland::zxdg_positioner_v6 *positioner)
    : zxdg_popup_v6(xdgSurface->get_popup(parent->object(), positioner->object()))
    , m_xdgSurface(xdgSurface)
{
}

QWaylandXdgSurfaceV6::Popup::~Popup()
{
    if (isInitialized())
        destroy();
}

void QWaylandXdgSurfaceV6::Popup::zxdg_popup_v6_popup_done()
{
    m_xdgSurface->m_window->window()->close();
}

QWaylandXdgSurfaceV6::QWaylandXdgSurfaceV6(QWaylandXdgShellV6 *shell, ::zxdg_surface_v6 *surface, QWaylandWindow *window)
                    : QWaylandShellSurface(window)
                    , zxdg_surface_v6(surface)
                    , m_shell(shell)
                    , m_window(window)
{
}

QWaylandXdgSurfaceV6::~QWaylandXdgSurfaceV6()
{
    if (m_toplevel)
        zxdg_toplevel_v6_destroy(m_toplevel->object());
    if (m_popup)
        zxdg_popup_v6_destroy(m_popup->object());
    destroy();
}

void QWaylandXdgSurfaceV6::resize(QWaylandInputDevice *inputDevice, zxdg_toplevel_v6_resize_edge edges)
{
    Q_ASSERT(m_toplevel && m_toplevel->isInitialized());
    m_toplevel->resize(inputDevice->wl_seat(), inputDevice->serial(), edges);
}

void QWaylandXdgSurfaceV6::resize(QWaylandInputDevice *inputDevice, enum wl_shell_surface_resize edges)
{
    auto xdgEdges = reinterpret_cast<enum zxdg_toplevel_v6_resize_edge const * const>(&edges);
    resize(inputDevice, *xdgEdges);
}


bool QWaylandXdgSurfaceV6::move(QWaylandInputDevice *inputDevice)
{
    if (m_toplevel && m_toplevel->isInitialized()) {
        m_toplevel->move(inputDevice->wl_seat(), inputDevice->serial());
        return true;
    }
    return false;
}

void QWaylandXdgSurfaceV6::setTitle(const QString &title)
{
    if (m_toplevel)
        m_toplevel->set_title(title);
}

void QWaylandXdgSurfaceV6::setAppId(const QString &appId)
{
    if (m_toplevel)
        m_toplevel->set_app_id(appId);
}

void QWaylandXdgSurfaceV6::setType(Qt::WindowType type, QWaylandWindow *transientParent)
{
    QWaylandDisplay *display = m_window->display();
    if ((type == Qt::Popup || type == Qt::ToolTip) && transientParent && display->lastInputDevice()) {
        setPopup(transientParent, display->lastInputDevice(), display->lastInputSerial(), type == Qt::Popup);
    } else {
        setToplevel();
        if (transientParent) {
            auto parentXdgSurface = static_cast<QWaylandXdgSurfaceV6 *>(transientParent->shellSurface());
            if (parentXdgSurface)
                m_toplevel->set_parent(parentXdgSurface->m_toplevel->object());
        }
    }
}

bool QWaylandXdgSurfaceV6::handleExpose(const QRegion &region)
{
    if (!m_configured && !region.isEmpty()) {
        m_exposeRegion = region;
        return true;
    }
    return false;
}

void QWaylandXdgSurfaceV6::applyConfigure()
{
    Q_ASSERT(m_pendingConfigureSerial != 0);

    if (m_toplevel)
        m_toplevel->applyConfigure();

    m_configured = true;
    ack_configure(m_pendingConfigureSerial);

    m_pendingConfigureSerial = 0;
}

bool QWaylandXdgSurfaceV6::wantsDecorations() const
{
    return m_toplevel && !(m_toplevel->m_pending.states & Qt::WindowFullScreen);
}

void QWaylandXdgSurfaceV6::requestWindowStates(Qt::WindowStates states)
{
    if (m_toplevel)
        m_toplevel->requestWindowStates(states);
    else
        qCWarning(lcQpaWayland) << "Non-toplevel surfaces can't request window states";
}

void QWaylandXdgSurfaceV6::setToplevel()
{
    Q_ASSERT(!m_toplevel && !m_popup);
    m_toplevel = new Toplevel(this);
}

void QWaylandXdgSurfaceV6::setPopup(QWaylandWindow *parent, QWaylandInputDevice *device, int serial, bool grab)
{
    Q_ASSERT(!m_toplevel && !m_popup);

    auto parentXdgSurface = static_cast<QWaylandXdgSurfaceV6 *>(parent->shellSurface());
    auto positioner = new QtWayland::zxdg_positioner_v6(m_shell->create_positioner());
    // set_popup expects a position relative to the parent
    QPoint transientPos = m_window->geometry().topLeft(); // this is absolute
    transientPos -= parent->geometry().topLeft();
    if (parent->decoration()) {
        transientPos.setX(transientPos.x() + parent->decoration()->margins().left());
        transientPos.setY(transientPos.y() + parent->decoration()->margins().top());
    }
    positioner->set_anchor_rect(transientPos.x(), transientPos.y(), 1, 1);
    positioner->set_anchor(QtWayland::zxdg_positioner_v6::anchor_top | QtWayland::zxdg_positioner_v6::anchor_left);
    positioner->set_gravity(QtWayland::zxdg_positioner_v6::gravity_bottom | QtWayland::zxdg_positioner_v6::gravity_right);
    positioner->set_size(m_window->geometry().width(), m_window->geometry().height());
    m_popup = new Popup(this, parentXdgSurface, positioner);
    positioner->destroy();
    delete positioner;
    if (grab) {
        m_popup->grab(device->wl_seat(), serial);
    }
}

void QWaylandXdgSurfaceV6::zxdg_surface_v6_configure(uint32_t serial)
{
    m_window->applyConfigureWhenPossible();
    m_pendingConfigureSerial = serial;
    if (!m_exposeRegion.isEmpty()) {
        QWindowSystemInterface::handleExposeEvent(m_window->window(), m_exposeRegion);
        m_exposeRegion = QRegion();
    }
}

QWaylandXdgShellV6::QWaylandXdgShellV6(struct ::wl_registry *registry, uint32_t id, uint32_t availableVersion)
                  : QtWayland::zxdg_shell_v6(registry, id, qMin(availableVersion, 1u))
{
}

QWaylandXdgShellV6::~QWaylandXdgShellV6()
{
    destroy();
}

QWaylandXdgSurfaceV6 *QWaylandXdgShellV6::getXdgSurface(QWaylandWindow *window)
{
    return new QWaylandXdgSurfaceV6(this, get_xdg_surface(window->object()), window);
}

void QWaylandXdgShellV6::zxdg_shell_v6_ping(uint32_t serial)
{
    pong(serial);
}

}

QT_END_NAMESPACE
