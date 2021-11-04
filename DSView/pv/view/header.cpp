/*
 * This file is part of the DSView project.
 * DSView is based on PulseView.
 *
 * Copyright (C) 2012 Joel Holdsworth <joel@airwebreathe.org.uk>
 * Copyright (C) 2013 DreamSourceLab <support@dreamsourcelab.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "header.h"
#include "view.h"

#include "../../extdef.h"
#include "trace.h"
#include "dsosignal.h"
#include "logicsignal.h"
#include "analogsignal.h"
#include "groupsignal.h"
#include "decodetrace.h"
#include "../sigsession.h"
#include "../device/devinst.h"

#include <assert.h>

 
 
#include <QColorDialog>
#include <QInputDialog>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QRect>
#include <QStyleOption>
#include <QApplication>

using namespace boost;
using namespace std;

namespace pv {
namespace view {

Header::Header(View &parent) :
	QWidget(&parent),
    _view(parent),
    _action_add_group(new QAction(tr("Add Group"), this)),
    _action_del_group(new QAction(tr("Del Group"), this))
{
    _moveFlag = false;
    _colorFlag = false;
    _nameFlag = false;
    nameEdit = new QLineEdit(this);
    nameEdit->setFixedWidth(100);
    nameEdit->hide();

	setMouseTracking(true);

    connect(_action_del_group, SIGNAL(triggered()),
        this, SLOT(on_action_del_group_triggered()));
    connect(_action_add_group, SIGNAL(triggered()),
        this, SLOT(on_action_add_group_triggered()));

    connect(nameEdit, SIGNAL(editingFinished()),
            this, SLOT(on_action_set_name_triggered()));

    retranslateUi();
}

void Header::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QWidget::changeEvent(event);
}

void Header::retranslateUi()
{
    update();
}


int Header::get_nameEditWidth()
{
    if (nameEdit->hasFocus())
        return nameEdit->width();
    else
        return 0;
}

pv::view::Trace* Header::get_mTrace(int &action, const QPoint &pt)
{
    const int w = width();
    const auto &traces = _view.get_traces(ALL_VIEW);

    for(auto &t : traces)
    {
        assert(t);

        if ((action = t->pt_in_rect(t->get_y(), w, pt)))
            return t;
    }

    return NULL;
}

void Header::paintEvent(QPaintEvent*)
{
    using pv::view::Trace;

    QStyleOption o;
    o.initFrom(this);
    QPainter painter(this);
    style()->drawPrimitive(QStyle::PE_Widget, &o, &painter, this);

	const int w = width();
    const auto &traces = _view.get_traces(ALL_VIEW);

    const bool dragging = !_drag_traces.empty();
    QColor fore(QWidget::palette().color(QWidget::foregroundRole()));
    fore.setAlpha(View::ForeAlpha);

    for(auto &t : traces)
	{
        assert(t);
        t->paint_label(painter, w, dragging ? QPoint(-1, -1) : _mouse_point, fore);
	}

	painter.end();
}

void Header::mouseDoubleClickEvent(QMouseEvent *event)
{
    assert(event);

    const auto  &traces = _view.get_traces(ALL_VIEW);

    if (event->button() & Qt::LeftButton) {
        _mouse_down_point = event->pos();

        // Save the offsets of any Traces which will be dragged
        for(auto &t : traces)
            if (t->selected())
                _drag_traces.push_back(
                    make_pair(t, t->get_v_offset()));

        // Select the Trace if it has been clicked
        for(auto &t : traces)
            if (t->mouse_double_click(width(), event->pos()))
                break;
    }

}

void Header::mousePressEvent(QMouseEvent *event)
{
	assert(event);

    const auto &traces = _view.get_traces(ALL_VIEW);
    int action;
    const bool instant = _view.session().get_instant();
    if (instant && _view.session().get_capture_state() == SigSession::Running) {
        return;
    }

	if (event->button() & Qt::LeftButton) {
		_mouse_down_point = event->pos();

        // Save the offsets of any Traces which will be dragged
        for(auto &t : traces)
            if (t->selected())
                _drag_traces.push_back(
                    make_pair(t, t->get_v_offset()));

        // Select the Trace if it has been clicked
        const auto mTrace = get_mTrace(action, event->pos());
        if (action == Trace::COLOR && mTrace) {
            _colorFlag = true;
        } else if (action == Trace::NAME && mTrace) {
            _nameFlag = true;
        } else if (action == Trace::LABEL && mTrace) {
            mTrace->select(true);
            if (~QApplication::keyboardModifiers() &
                Qt::ControlModifier)
                _drag_traces.clear();
            _drag_traces.push_back(make_pair(mTrace, mTrace->get_zero_vpos()));
            mTrace->set_old_v_offset(mTrace->get_v_offset());
        }

        for(auto &t : traces)
            if (t->mouse_press(width(), event->pos()))
                break;

        if (~QApplication::keyboardModifiers() & Qt::ControlModifier) {
            // Unselect all other Traces because the Ctrl is not
            // pressed
            for(auto &t : traces)
                if (t != mTrace)
                    t->select(false);
        }
        update();
    }
}

void Header::mouseReleaseEvent(QMouseEvent *event)
{
	assert(event);

    // judge for color / name / trigger / move
    int action;
    const auto mTrace = get_mTrace(action, event->pos());
    if (mTrace){
        if (action == Trace::COLOR && _colorFlag) {
            _context_trace = mTrace;
            changeColor(event);
            _view.set_all_update(true);
        } else if (action == Trace::NAME && _nameFlag) {
            _context_trace = mTrace;
            changeName(event);
        }
    }
    if (_moveFlag) {
        //move(event);
        _drag_traces.clear();
        _view.signals_changed();
        _view.set_all_update(true);

        const auto &traces = _view.get_traces(ALL_VIEW);

        for(auto &t : traces){
            t->select(false);
        }            
    }

    _colorFlag = false;
    _nameFlag = false;
    _moveFlag = false;

    _view.normalize_layout();
}

void Header::wheelEvent(QWheelEvent *event)
{
    assert(event);

    if (event->orientation() == Qt::Vertical) {
        const auto &traces = _view.get_traces(ALL_VIEW);
        // Vertical scrolling
        double shift = 0;
        #ifdef Q_OS_DARWIN
        static bool active = true;
        static int64_t last_time;
        if (event->source() == Qt::MouseEventSynthesizedBySystem) {
            if (active) {
                last_time = QDateTime::currentMSecsSinceEpoch();
                shift = event->delta() > 1.5 ? -1 :
                        event->delta() < -1.5 ? 1 : 0;
            }
            int64_t cur_time = QDateTime::currentMSecsSinceEpoch();
            if (cur_time - last_time > 100)
                active = true;
            else
                active = false;
        } else {
            shift = -event->delta() / 80.0;
        }
        #else
            shift = event->delta() / 80.0;
        #endif
        for(auto &t : traces)
            if (t->mouse_wheel(width(), event->pos(), shift))
                break;
        update();
    }
}

void Header::changeName(QMouseEvent *event)
{
    if ((event->button() == Qt::LeftButton) &&
        (_context_trace->get_type() != SR_CHANNEL_DSO)) {
        header_resize();
        nameEdit->setText(_context_trace->get_name());
        nameEdit->selectAll();
        nameEdit->setFocus();
        nameEdit->show();
        header_updated();
    }
}

void Header::changeColor(QMouseEvent *event)
{
    if ((event->button() == Qt::LeftButton)) {
        const QColor new_color = QColorDialog::getColor(_context_trace->get_colour(), this, tr("Set Channel Colour"));
        if (new_color.isValid())
            _context_trace->set_colour(new_color);
    }
}

void Header::mouseMoveEvent(QMouseEvent *event)
{
	assert(event);
	_mouse_point = event->pos();

    // Move the Traces if we are dragging
    if (!_drag_traces.empty()) {
		const int delta = event->pos().y() - _mouse_down_point.y();

        for (auto i = _drag_traces.begin(); i != _drag_traces.end(); i++) {
            const auto sig = (*i).first;
			if (sig) {
                int y = (*i).second + delta;
                if (sig->get_type() == SR_CHANNEL_DSO) {
                    DsoSignal *dsoSig = NULL;
                    if ((dsoSig = dynamic_cast<DsoSignal*>(sig))) {
                        dsoSig->set_zero_vpos(y);
                        _moveFlag = true;
                        traces_moved();
                    }
                } else if (sig->get_type() == SR_CHANNEL_MATH) {
                    MathTrace *mathTrace = NULL;
                    if ((mathTrace = dynamic_cast<MathTrace*>(sig))) {
                       mathTrace->set_zero_vpos(y);
                       _moveFlag = true;
                       traces_moved();
                    }
                 } else if (sig->get_type() == SR_CHANNEL_ANALOG) {
                    AnalogSignal *analogSig = NULL;
                    if ((analogSig = dynamic_cast<AnalogSignal*>(sig))) {
                        analogSig->set_zero_vpos(y);
                        _moveFlag = true;
                        traces_moved();
                    }
                } else {
                    if (~QApplication::keyboardModifiers() & Qt::ControlModifier) {
                        const int y_snap =
                            ((y + View::SignalSnapGridSize / 2) /
                                View::SignalSnapGridSize) *
                                View::SignalSnapGridSize;
                        if (y_snap != sig->get_v_offset()) {
                            _moveFlag = true;
                            sig->set_v_offset(y_snap);
                        }
                    }
                }
			}
		}
	}
	update();
}

void Header::leaveEvent(QEvent*)
{
	_mouse_point = QPoint(-1, -1);
	update();
}

void Header::contextMenuEvent(QContextMenuEvent *event)
{
    (void)event;

    int action;

    const auto t = get_mTrace(action, _mouse_point);

    if (!t || !t->selected() || action != Trace::LABEL)
        return;

    /*
     * disable group function for v0.97 temporarily
     */
//    QMenu menu(this);
//    if (t->get_type() == SR_CHANNEL_LOGIC)
//        menu.addAction(_action_add_group);
//    else if (t->get_type() == SR_CHANNEL_GROUP)
//        menu.addAction(_action_del_group);

//    _context_trace = t;
//    menu.exec(event->globalPos());
//    _context_trace.r-eset();
}

void Header::on_action_set_name_triggered()
{
    auto context_Trace = _context_trace;
    if (!context_Trace)
		return;

    if (nameEdit->isModified()) {
        context_Trace->set_name(nameEdit->text());
        if (context_Trace->get_type() == SR_CHANNEL_LOGIC ||
                context_Trace->get_type() == SR_CHANNEL_ANALOG)
            sr_dev_probe_name_set(_view.session().get_device()->dev_inst(), context_Trace->get_index(), nameEdit->text().toUtf8().constData());
    }

    nameEdit->hide();
    header_updated();
}

void Header::on_action_add_group_triggered()
{
    _view.session().add_group();
}

void Header::on_action_del_group_triggered()
{
    _view.session().del_group();
}

void Header::header_resize()
{
    //if (nameEdit->isVisible()) {
    if (_context_trace) {
        const int y = _context_trace->get_y();
        nameEdit->move(QPoint(_context_trace->get_leftWidth(), y - nameEdit->height() / 2));
    }
}


} // namespace view
} // namespace pv
