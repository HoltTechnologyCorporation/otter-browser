/**************************************************************************
* Otter Browser: Web browser controlled by the user, not vice-versa.
* Copyright (C) 2013 - 2017 Michal Dutkiewicz aka Emdek <michal@emdek.pl>
* Copyright (C) 2015 - 2016 Piotr Wójcik <chocimier@tlen.pl>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*
**************************************************************************/

#include "GesturesManager.h"
#include "ActionsManager.h"
#include "IniSettings.h"
#include "SessionsManager.h"
#include "SettingsManager.h"

#include <QtCore/QMetaEnum>
#include <QtCore/QPointer>
#include <QtCore/QRegularExpression>
#include <QtWidgets/QApplication>

#include <limits>

#define UNKNOWN_GESTURE -1
#define NATIVE_GESTURE -2

namespace Otter
{

GesturesManager::GestureStep::GestureStep() : type(QEvent::None),
	button(Qt::NoButton),
	modifiers(Qt::NoModifier),
	direction(MouseGestures::UnknownMouseAction)
{
}

GesturesManager::GestureStep::GestureStep(QEvent::Type type, MouseGestures::MouseAction direction, Qt::KeyboardModifiers modifier) : type(type),
	button(Qt::NoButton),
	modifiers(modifier),
	direction(direction)
{
}

GesturesManager::GestureStep::GestureStep(QEvent::Type type, Qt::MouseButton button, Qt::KeyboardModifiers modifier) : type(type),
	button(button),
	modifiers(modifier),
	direction(MouseGestures::UnknownMouseAction)
{
}

GesturesManager::GestureStep::GestureStep(const QInputEvent *event) : type(event->type()),
	button(Qt::NoButton),
	modifiers(event->modifiers()),
	direction(MouseGestures::UnknownMouseAction)
{
	switch (event->type())
	{
		case QEvent::MouseButtonPress:
		case QEvent::MouseButtonRelease:
		case QEvent::MouseButtonDblClick:
			{
				const QMouseEvent *mouseEvent(static_cast<const QMouseEvent*>(event));

				if (mouseEvent)
				{
					button = mouseEvent->button();
				}
			}

			break;
		case QEvent::Wheel:
			{
				const QWheelEvent *wheelEvent(static_cast<const QWheelEvent*>(event));

				if (wheelEvent)
				{
					const QPoint delta(wheelEvent->angleDelta());

					if (qAbs(delta.x()) > qAbs(delta.y()))
					{
						direction = (delta.x() > 0) ? MouseGestures::MoveRightMouseAction : MouseGestures::MoveLeftMouseAction;
					}
					else if (qAbs(delta.y()) > 0)
					{
						direction = (delta.y() > 0) ? MouseGestures::MoveUpMouseAction : MouseGestures::MoveDownMouseAction;
					}
				}
			}

			break;
		default:
			break;
	}
}

QString GesturesManager::GestureStep::toString() const
{
	QString string;

	switch (type)
	{
		case QEvent::MouseButtonPress:
			string += QLatin1String("press");

			break;
		case QEvent::MouseButtonRelease:
			string += QLatin1String("release");

			break;
		case QEvent::MouseButtonDblClick:
			string += QLatin1String("doubleClick");

			break;
		case QEvent::Wheel:
			string += QLatin1String("scroll");

			break;
		case QEvent::MouseMove:
			string += QLatin1String("move");

			break;
		default:
			break;
	}

	if (type == QEvent::Wheel || type == QEvent::MouseMove)
	{
		switch (direction)
		{
			case MouseGestures::MoveUpMouseAction:
				string += QLatin1String("Up");

				break;
			case MouseGestures::MoveDownMouseAction:
				string += QLatin1String("Down");

				break;
			case MouseGestures::MoveLeftMouseAction:
				string += QLatin1String("Left");

				break;
			case MouseGestures::MoveRightMouseAction:
				string += QLatin1String("Right");

				break;
			case MouseGestures::MoveHorizontallyMouseAction:
				string += QLatin1String("Horizontal");

				break;
			case MouseGestures::MoveVerticallyMouseAction:
				string += QLatin1String("Vertical");

				break;
			default:
				break;
		}
	}
	else
	{
		switch (button)
		{
			case Qt::LeftButton:
				string += QLatin1String("Left");

				break;
			case Qt::RightButton:
				string += QLatin1String("Right");

				break;
			case Qt::MiddleButton:
				string += QLatin1String("Middle");

				break;
			case Qt::BackButton:
				string += QLatin1String("Back");

				break;
			case Qt::ForwardButton:
				string += QLatin1String("Forward");

				break;
			case Qt::TaskButton:
				string += QLatin1String("Task");

				break;
			default:
				break;
		}

		for (int i = 4; i <= 24; ++i)
		{
			if (button == static_cast<Qt::MouseButton>(Qt::ExtraButton1 << (i - 1)))
			{
				string += QStringLiteral("Extra%1").arg(i);
			}
		}
	}

	if (modifiers.testFlag(Qt::ShiftModifier))
	{
		string += QLatin1String("+shift");
	}

	if (modifiers.testFlag(Qt::ControlModifier))
	{
		string += QLatin1String("+ctrl");
	}

	if (modifiers.testFlag(Qt::AltModifier))
	{
		string += QLatin1String("+alt");
	}

	if (modifiers.testFlag(Qt::MetaModifier))
	{
		string += QLatin1String("+meta");
	}

	return string;
}

bool GesturesManager::GestureStep::operator ==(const GestureStep &other) const
{
	return (type == other.type) && (button == other.button) && (direction == other.direction) && (modifiers == other.modifiers || type == QEvent::MouseMove);
}

bool GesturesManager::GestureStep::operator !=(const GestureStep &other) const
{
	return !((*this) == other);
}

GesturesManager* GesturesManager::m_instance(nullptr);
MouseGestures::Recognizer* GesturesManager::m_recognizer(nullptr);
QPointer<QObject> GesturesManager::m_trackedObject(nullptr);
QPoint GesturesManager::m_lastClick;
QPoint GesturesManager::m_lastPosition;
QVariantMap GesturesManager::m_paramaters;
QHash<GesturesManager::GesturesContext, QVector<GesturesManager::MouseGesture> > GesturesManager::m_gestures;
QHash<GesturesManager::GesturesContext, QVector<QVector<GesturesManager::GestureStep> > > GesturesManager::m_nativeGestures;
QVector<QInputEvent*> GesturesManager::m_events;
QVector<GesturesManager::GestureStep> GesturesManager::m_steps;
QVector<GesturesManager::GesturesContext> GesturesManager::m_contexts;
int GesturesManager::m_gesturesContextEnumerator(0);
bool GesturesManager::m_isReleasing(false);
bool GesturesManager::m_afterScroll(false);

GesturesManager::GesturesManager(QObject *parent) : QObject(parent),
	m_reloadTimer(0)
{
	connect(SettingsManager::getInstance(), SIGNAL(optionChanged(int,QVariant)), this, SLOT(handleOptionChanged(int)));
}

void GesturesManager::createInstance(QObject *parent)
{
	if (!m_instance)
	{
		QVector<QVector<GestureStep> > generic;
		generic.reserve(3);
		generic.append(QVector<GestureStep>({GestureStep(QEvent::MouseButtonDblClick, Qt::LeftButton)}));
		generic.append(QVector<GestureStep>({GestureStep(QEvent::MouseButtonPress, Qt::LeftButton), GestureStep(QEvent::MouseButtonRelease, Qt::LeftButton)}));
		generic.append(QVector<GestureStep>({GestureStep(QEvent::MouseButtonPress, Qt::LeftButton), GestureStep(QEvent::MouseMove, MouseGestures::UnknownMouseAction)}));

		QVector<QVector<GestureStep> > link;
		link.append(QVector<GestureStep>({GestureStep(QEvent::MouseButtonPress, Qt::LeftButton), GestureStep(QEvent::MouseButtonRelease, Qt::LeftButton)}));
		link.append(QVector<GestureStep>({GestureStep(QEvent::MouseButtonPress, Qt::LeftButton), GestureStep(QEvent::MouseMove, MouseGestures::UnknownMouseAction)}));

		QVector<QVector<GestureStep> > contentEditable;
		contentEditable.append(QVector<GestureStep>({GestureStep(QEvent::MouseButtonPress, Qt::MiddleButton)}));

		QVector<QVector<GestureStep> > tabHandle;
		tabHandle.append(QVector<GestureStep>({GestureStep(QEvent::MouseButtonPress, Qt::LeftButton), GestureStep(QEvent::MouseMove, MouseGestures::UnknownMouseAction)}));

		m_nativeGestures[GesturesManager::GenericContext] = generic;
		m_nativeGestures[GesturesManager::LinkContext] = link;
		m_nativeGestures[GesturesManager::ContentEditableContext] = contentEditable;
		m_nativeGestures[GesturesManager::TabHandleContext] = tabHandle;

		m_instance = new GesturesManager(parent);
		m_gesturesContextEnumerator = m_instance->metaObject()->indexOfEnumerator(QLatin1String("GesturesContext").data());

		loadProfiles();
	}
}

void GesturesManager::timerEvent(QTimerEvent *event)
{
	if (event->timerId() == m_reloadTimer)
	{
		killTimer(m_reloadTimer);

		m_reloadTimer = 0;

		loadProfiles();
	}
}

void GesturesManager::loadProfiles()
{
	m_gestures.clear();

	MouseGesture contextMenuGestureDefinition;
	contextMenuGestureDefinition.steps = QVector<GestureStep>({GestureStep(QEvent::MouseButtonPress, Qt::RightButton), GestureStep(QEvent::MouseButtonRelease, Qt::RightButton)});
	contextMenuGestureDefinition.action = ActionsManager::ContextMenuAction;

	for (int i = (UnknownContext + 1); i < OtherContext; ++i)
	{
		GesturesContext context(static_cast<GesturesContext>(i));

		m_gestures[context] = QVector<MouseGesture>();
		m_gestures[context].append(contextMenuGestureDefinition);
	}

	const QStringList gestureProfiles(SettingsManager::getOption(SettingsManager::Browser_MouseProfilesOrderOption).toStringList());
	const bool areMouseGesturesEnabled(SettingsManager::getOption(SettingsManager::Browser_EnableMouseGesturesOption).toBool());

	for (int i = 0; i < gestureProfiles.count(); ++i)
	{
		IniSettings profile(SessionsManager::getReadableDataPath(QLatin1String("mouse/") + gestureProfiles.at(i) + QLatin1String(".ini")));
		const QStringList contexts(profile.getGroups());

		for (int j = 0; j < contexts.count(); ++j)
		{
			const GesturesContext context(static_cast<GesturesContext>(m_instance->metaObject()->enumerator(m_gesturesContextEnumerator).keyToValue((contexts.at(j) + QLatin1String("Context")).toLatin1())));

			if (context == UnknownContext)
			{
				profile.endGroup();

				continue;
			}

			profile.beginGroup(contexts.at(j));

			const QStringList gestures(profile.getKeys());

			for (int k = 0; k < gestures.count(); ++k)
			{
				const QStringList rawMouseActions(gestures.at(k).split(QLatin1Char(',')));
				int action(ActionsManager::getActionIdentifier(profile.getValue(gestures.at(k), QString()).toString()));

				if (rawMouseActions.isEmpty())
				{
					continue;
				}

				if (action < 0)
				{
					if (profile.getValue(gestures.at(k)) == QLatin1String("NoAction"))
					{
						action = NATIVE_GESTURE;
					}
					else
					{
						continue;
					}
				}

				QVector<GestureStep> steps;
				steps.reserve(rawMouseActions.count());

				bool hasMove(false);

				for (int l = 0; l < rawMouseActions.count(); ++l)
				{
					steps.append(deserializeStep(rawMouseActions.at(l)));

					if (steps.last().type == QEvent::MouseMove)
					{
						hasMove = true;
					}
				}

				if (!steps.empty() && (!hasMove || areMouseGesturesEnabled))
				{
					MouseGesture definition;
					definition.steps = steps;
					definition.action = action;

					m_gestures[context].append(definition);
				}
			}

			profile.endGroup();
		}
	}
}

void GesturesManager::recognizeMoveStep(QInputEvent *event)
{
	if (!m_recognizer)
	{
		return;
	}

	QHash<int, MouseGestures::ActionList> possibleMoves;

	for (int i = 0; i < m_contexts.count(); ++i)
	{
		for (int j = 0; j < m_gestures[m_contexts[i]].count(); ++j)
		{
			const QVector<GestureStep> steps(m_gestures[m_contexts[i]][j].steps);

			if (steps.count() > m_steps.count() && steps[m_steps.count()].type == QEvent::MouseMove && steps.mid(0, m_steps.count()) == m_steps)
			{
				MouseGestures::ActionList moves;

				for (int k = m_steps.count(); k < steps.count() && steps[k].type == QEvent::MouseMove; ++k)
				{
					moves.push_back(steps[k].direction);
				}

				if (!moves.empty())
				{
					possibleMoves.insert(m_recognizer->registerGesture(moves), moves);
				}
			}
		}
	}

	const QMouseEvent *mouseEvent(static_cast<const QMouseEvent*>(event));

	if (mouseEvent)
	{
		m_recognizer->addPosition(mouseEvent->pos().x(), mouseEvent->pos().y());
	}

	const int gesture(m_recognizer->endGesture());
	const MouseGestures::ActionList moves(possibleMoves.value(gesture));
	MouseGestures::ActionList::const_iterator iterator;

	for (iterator = moves.begin(); iterator != moves.end(); ++iterator)
	{
		m_steps.append(GestureStep(QEvent::MouseMove, *iterator, event->modifiers()));
	}

	if (m_steps.empty() && calculateLastMoveDistance(true) >= QApplication::startDragDistance())
	{
		m_steps.append(GestureStep(QEvent::MouseMove, MouseGestures::UnknownMouseAction, event->modifiers()));
	}
}

void GesturesManager::cancelGesture()
{
	releaseObject();

	m_steps.clear();

	qDeleteAll(m_events);

	m_events.clear();
}

void GesturesManager::releaseObject()
{
	if (m_trackedObject)
	{
		m_trackedObject->removeEventFilter(m_instance);

		disconnect(m_trackedObject, SIGNAL(destroyed(QObject*)), m_instance, SLOT(endGesture()));
	}

	m_trackedObject = nullptr;
}

void GesturesManager::endGesture()
{
	cancelGesture();
}

void GesturesManager::handleOptionChanged(int identifier)
{
	switch (identifier)
	{
		case SettingsManager::Browser_EnableMouseGesturesOption:
		case SettingsManager::Browser_MouseProfilesOrderOption:
			if (m_reloadTimer == 0)
			{
				m_reloadTimer = startTimer(250);
			}

			break;
		default:
			break;
	}
}

GesturesManager* GesturesManager::getInstance()
{
	return m_instance;
}

QObject* GesturesManager::getTrackedObject()
{
	return m_trackedObject;
}

GesturesManager::GestureStep GesturesManager::deserializeStep(const QString &string)
{
	GestureStep step;
	const QStringList parts(string.split(QLatin1Char('+')));
	const QString event(parts.first());

	if (event.startsWith(QLatin1String("press")))
	{
		step.type = QEvent::MouseButtonPress;
	}
	else if (event.startsWith(QLatin1String("release")))
	{
		step.type = QEvent::MouseButtonRelease;
	}
	else if (event.startsWith(QLatin1String("doubleClick")))
	{
		step.type = QEvent::MouseButtonDblClick;
	}
	else if (event.startsWith(QLatin1String("scroll")))
	{
		step.type = QEvent::Wheel;
	}
	else if (event.startsWith(QLatin1String("move")))
	{
		step.type = QEvent::MouseMove;
	}

	if (step.type == QEvent::Wheel || step.type == QEvent::MouseMove)
	{
		if (event.endsWith(QLatin1String("Up")))
		{
			step.direction = MouseGestures::MoveUpMouseAction;
		}
		else if (event.endsWith(QLatin1String("Down")))
		{
			step.direction = MouseGestures::MoveDownMouseAction;
		}
		else if (event.endsWith(QLatin1String("Left")))
		{
			step.direction = MouseGestures::MoveLeftMouseAction;
		}
		else if (event.endsWith(QLatin1String("Right")))
		{
			step.direction = MouseGestures::MoveRightMouseAction;
		}
		else if (event.endsWith(QLatin1String("Horizontal")))
		{
			step.direction = MouseGestures::MoveHorizontallyMouseAction;
		}
		else if (event.endsWith(QLatin1String("Vertical")))
		{
			step.direction = MouseGestures::MoveVerticallyMouseAction;
		}
	}
	else
	{
		if (event.endsWith(QLatin1String("Left")))
		{
			step.button = Qt::LeftButton;
		}
		else if (event.endsWith(QLatin1String("Right")))
		{
			step.button = Qt::RightButton;
		}
		else if (event.endsWith(QLatin1String("Middle")))
		{
			step.button = Qt::MiddleButton;
		}
		else if (event.endsWith(QLatin1String("Back")))
		{
			step.button = Qt::BackButton;
		}
		else if (event.endsWith(QLatin1String("Forward")))
		{
			step.button = Qt::ForwardButton;
		}
		else if (event.endsWith(QLatin1String("Task")))
		{
			step.button = Qt::TaskButton;
		}
		else
		{
			const int number(QRegularExpression(QLatin1String("Extra(\\d{1,2})$")).match(event).captured(1).toInt());

			if (1 <= number && number <= 24)
			{
				step.button = static_cast<Qt::MouseButton>(Qt::ExtraButton1 << (number - 1));
			}
		}
	}

	for (int i = 1; i < parts.count(); ++i)
	{
		if (parts.at(i) == QLatin1String("shift"))
		{
			step.modifiers |= Qt::ShiftModifier;
		}
		else if (parts.at(i) == QLatin1String("ctrl"))
		{
			step.modifiers |= Qt::ControlModifier;
		}
		else if (parts.at(i) == QLatin1String("alt"))
		{
			step.modifiers |= Qt::AltModifier;
		}
		else if (parts.at(i) == QLatin1String("meta"))
		{
			step.modifiers |= Qt::MetaModifier;
		}
	}

	return step;
}

int GesturesManager::matchGesture()
{
	int bestGesture(0);
	int lowestDifference(std::numeric_limits<int>::max());

	for (int i = 0; i < m_contexts.count(); ++i)
	{
		for (int j = 0; j < m_nativeGestures[m_contexts[i]].count(); ++j)
		{
			const int difference(calculateGesturesDifference(m_nativeGestures[m_contexts[i]][j]));

			if (difference == 0)
			{
				return NATIVE_GESTURE;
			}

			if (difference < lowestDifference)
			{
				bestGesture = NATIVE_GESTURE;
				lowestDifference = difference;
			}
		}

		for (int j = 0; j < m_gestures[m_contexts[i]].count(); ++j)
		{
			const int difference(calculateGesturesDifference(m_gestures[m_contexts[i]][j].steps));

			if (difference == 0)
			{
				return m_gestures[m_contexts[i]][j].action;
			}

			if (difference < lowestDifference)
			{
				bestGesture = m_gestures[m_contexts[i]][j].action;
				lowestDifference = difference;
			}
		}
	}

	return ((lowestDifference < std::numeric_limits<int>::max()) ? bestGesture : UNKNOWN_GESTURE);
}

int GesturesManager::calculateLastMoveDistance(bool measureFinished)
{
	int result(0);
	int index(m_events.count() - 1);

	if (!measureFinished && (index < 0 || m_events[index]->type() != QEvent::MouseMove))
	{
		return result;
	}

	while (index >= 0 && m_events[index]->type() != QEvent::MouseMove)
	{
		--index;
	}

	for (; index > 0 && m_events[index - 1]->type() == QEvent::MouseMove; --index)
	{
		QMouseEvent *current(static_cast<QMouseEvent*>(m_events[index]));
		QMouseEvent *previous(static_cast<QMouseEvent*>(m_events[index - 1]));

		if (!current || !previous)
		{
			break;
		}

		result += (previous->pos() - current->pos()).manhattanLength();
	}

	return result;
}

int GesturesManager::calculateGesturesDifference(const QVector<GestureStep> &steps)
{
	if (m_steps.count() != steps.count())
	{
		return std::numeric_limits<int>::max();
	}

	int difference(0);

	for (int j = 0; j < steps.count(); ++j)
	{
		int stepDifference(0);

		if (j == (steps.count() - 1) && steps[j].type == QEvent::MouseButtonPress && m_steps[j].type == QEvent::MouseButtonDblClick && steps[j].button == m_steps[j].button && steps[j].modifiers == m_steps[j].modifiers)
		{
			stepDifference += 100;
		}

		if (m_steps[j].type == steps[j].type && (steps[j].type == QEvent::MouseButtonPress || steps[j].type == QEvent::MouseButtonRelease || steps[j].type == QEvent::MouseButtonDblClick) && m_steps[j].button == steps[j].button && (m_steps[j].modifiers | steps[j].modifiers) == m_steps[j].modifiers)
		{
			if (m_steps[j].modifiers.testFlag(Qt::ControlModifier) && !steps[j].modifiers.testFlag(Qt::ControlModifier))
			{
				stepDifference += 8;
			}

			if (m_steps[j].modifiers.testFlag(Qt::ShiftModifier) && !steps[j].modifiers.testFlag(Qt::ShiftModifier))
			{
				stepDifference += 4;
			}

			if (m_steps[j].modifiers.testFlag(Qt::AltModifier) && !steps[j].modifiers.testFlag(Qt::AltModifier))
			{
				stepDifference += 2;
			}

			if (m_steps[j].modifiers.testFlag(Qt::MetaModifier) && !steps[j].modifiers.testFlag(Qt::MetaModifier))
			{
				stepDifference += 1;
			}
		}

		if (stepDifference == 0 && steps[j] != m_steps[j])
		{
			return std::numeric_limits<int>::max();
		}

		difference += stepDifference;
	}

	return difference;
}

bool GesturesManager::startGesture(QObject *object, QEvent *event, QVector<GesturesContext> contexts, const QVariantMap &parameters)
{
	QInputEvent *inputEvent(static_cast<QInputEvent*>(event));

	if (!object || !inputEvent || m_gestures.keys().toSet().intersect(contexts.toList().toSet()).isEmpty() || m_events.contains(inputEvent))
	{
		return false;
	}

	m_paramaters = parameters;

	if (!m_trackedObject)
	{
		m_contexts = contexts;
		m_isReleasing = false;
		m_afterScroll = false;
	}

	createInstance();
	releaseObject();

	m_trackedObject = object;

	if (m_trackedObject)
	{
		m_trackedObject->installEventFilter(m_instance);

		connect(m_trackedObject, SIGNAL(destroyed(QObject*)), m_instance, SLOT(endGesture()));
	}

	return (m_trackedObject && m_instance->eventFilter(m_trackedObject, event));
}

bool GesturesManager::continueGesture(QObject *object)
{
	if (!m_trackedObject)
	{
		return false;
	}

	if (!object)
	{
		cancelGesture();

		return false;
	}

	releaseObject();

	m_trackedObject = object;
	m_trackedObject->installEventFilter(m_instance);

	connect(m_trackedObject, SIGNAL(destroyed(QObject*)), m_instance, SLOT(endGesture()));

	return true;
}

bool GesturesManager::triggerAction(int gestureIdentifier)
{
	if (gestureIdentifier == UNKNOWN_GESTURE)
	{
		return false;
	}

	m_trackedObject->removeEventFilter(m_instance);

	if (gestureIdentifier == NATIVE_GESTURE)
	{
		for (int i = 0; i < m_events.count(); ++i)
		{
			QCoreApplication::sendEvent(m_trackedObject, m_events[i]);
		}

		cancelGesture();
	}
	else if (gestureIdentifier == ActionsManager::ContextMenuAction)
	{
		if (m_trackedObject)
		{
			QContextMenuEvent event(QContextMenuEvent::Other, m_lastPosition);

			QCoreApplication::sendEvent(m_trackedObject, &event);
		}
	}
	else
	{
		ActionsManager::triggerAction(gestureIdentifier, m_trackedObject, m_paramaters);
	}

	if (m_trackedObject)
	{
		m_trackedObject->installEventFilter(m_instance);
	}

	return true;
}

bool GesturesManager::isTracking()
{
	return (m_trackedObject != nullptr);
}

bool GesturesManager::eventFilter(QObject *object, QEvent *event)
{
	QMouseEvent *mouseEvent(nullptr);
	int gesture(UNKNOWN_GESTURE);

	switch (event->type())
	{
		case QEvent::MouseButtonPress:
		case QEvent::MouseButtonRelease:
		case QEvent::MouseButtonDblClick:
			mouseEvent = static_cast<QMouseEvent*>(event);

			if (!mouseEvent)
			{
				break;
			}

			if (!m_events.isEmpty() && m_events.last()->type() == event->type())
			{
				QMouseEvent *previousMouseEvent(static_cast<QMouseEvent*>(m_events.last()));

				if (previousMouseEvent && previousMouseEvent->button() == mouseEvent->button() && previousMouseEvent->modifiers() == mouseEvent->modifiers())
				{
					break;
				}
			}

			m_events.append(new QMouseEvent(event->type(), mouseEvent->localPos(), mouseEvent->windowPos(), mouseEvent->screenPos(), mouseEvent->button(), mouseEvent->buttons(), mouseEvent->modifiers()));

			if (m_afterScroll && event->type() == QEvent::MouseButtonRelease)
			{
				break;
			}

			m_lastPosition = mouseEvent->pos();
			m_lastClick = mouseEvent->pos();

			recognizeMoveStep(mouseEvent);

			m_steps.append(GestureStep(mouseEvent));

			if (m_isReleasing && event->type() == QEvent::MouseButtonRelease)
			{
				for (int i = (m_steps.count() - 1); i >= 0; --i)
				{
					if (m_steps[i].button == mouseEvent->button())
					{
						m_steps.removeAt(i);
					}
				}
			}
			else
			{
				m_isReleasing = false;
			}

			if (m_recognizer)
			{
				delete m_recognizer;

				m_recognizer = nullptr;
			}

			gesture = matchGesture();

			if (triggerAction(gesture))
			{
				m_isReleasing = true;
			}

			m_afterScroll = false;

			break;
		case QEvent::MouseMove:
			mouseEvent = static_cast<QMouseEvent*>(event);

			if (!mouseEvent)
			{
				break;
			}

			m_events.append(new QMouseEvent(event->type(), mouseEvent->localPos(), mouseEvent->windowPos(), mouseEvent->screenPos(), mouseEvent->button(), mouseEvent->buttons(), mouseEvent->modifiers()));

			m_afterScroll = false;

			if (!m_recognizer)
			{
				m_recognizer = new MouseGestures::Recognizer();
				m_recognizer->startGesture(m_lastClick.x(), m_lastClick.y());
			}

			m_lastPosition = mouseEvent->pos();

			m_recognizer->addPosition(mouseEvent->pos().x(), mouseEvent->pos().y());

			if (calculateLastMoveDistance() >= QApplication::startDragDistance())
			{
				m_steps.append(GestureStep(QEvent::MouseMove, MouseGestures::UnknownMouseAction, mouseEvent->modifiers()));

				gesture = matchGesture();

				if (gesture != UNKNOWN_GESTURE)
				{
					recognizeMoveStep(mouseEvent);

					triggerAction(gesture);
				}
				else
				{
					m_steps.pop_back();
				}
			}

			break;
		case QEvent::Wheel:
			{
				QWheelEvent *wheelEvent(static_cast<QWheelEvent*>(event));

				if (!wheelEvent)
				{
					break;
				}

				m_events.append(new QWheelEvent(wheelEvent->pos(), wheelEvent->globalPos(), wheelEvent->pixelDelta(), wheelEvent->angleDelta(), wheelEvent->delta(), wheelEvent->orientation(), wheelEvent->buttons(), wheelEvent->modifiers()));

				recognizeMoveStep(wheelEvent);

				m_steps.append(GestureStep(wheelEvent));

				m_lastClick = wheelEvent->pos();

				if (m_recognizer)
				{
					delete m_recognizer;

					m_recognizer = nullptr;
				}

				gesture = matchGesture();

				triggerAction(gesture);

				while (!m_steps.isEmpty() && m_steps[m_steps.count() - 1].type == QEvent::Wheel)
				{
					m_steps.removeAt(m_steps.count() - 1);
				}

				while (!m_events.isEmpty() && m_events[m_events.count() - 1]->type() == QEvent::Wheel)
				{
					m_events.removeAt(m_events.count() - 1);
				}

				m_afterScroll = true;

				break;
			}
		default:
			return QObject::eventFilter(object, event);
	}

	if (m_trackedObject && mouseEvent && mouseEvent->buttons() == Qt::NoButton)
	{
		cancelGesture();
	}

	return (!m_steps.isEmpty() || gesture != UNKNOWN_GESTURE);
}

}
