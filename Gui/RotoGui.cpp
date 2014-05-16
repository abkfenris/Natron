//  Natron
//
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/*
*Created by Alexandre GAUTHIER-FOICHAT on 6/1/2012.
*contact: immarespond at gmail dot com
*
*/

#include "RotoGui.h"

#include <QString>
#include <QToolBar>
#include <QWidget>
#include <QAction>
#include <QRectF>
#include <QLineF>
#include <QKeyEvent>
#include <QHBoxLayout>

#include "Engine/Node.h"
#include "Engine/RotoContext.h"
#include "Engine/TimeLine.h"

#include "Gui/FromQtEnums.h"
#include "Gui/NodeGui.h"
#include "Gui/Button.h"
#include "Gui/ViewerTab.h"
#include "Gui/ViewerGL.h"
#include "Gui/GuiAppInstance.h"

#include "Global/GLIncludes.h"

#define kControlPointMidSize 3
#define kBezierSelectionTolerance 10
#define kControlPointSelectionTolerance 8
#define kXHairSelectedCpsTolerance 10
#define kXHairSelectedCpsBox 8
#define kTangentHandleSelectionTolerance 8

using namespace Natron;

namespace {
    
static const double pi=3.14159265358979323846264338327950288419717;

///A list of points and their counter-part, that is: either a control point and its feather point, or
///the feather point and its associated control point
typedef std::pair<boost::shared_ptr<BezierCP> ,boost::shared_ptr<BezierCP> > SelectedCP;
typedef std::list< SelectedCP > SelectedCPs;
    
typedef std::list< boost::shared_ptr<Bezier> > SelectedBeziers;
    
enum EventState
{
    NONE = 0,
    DRAGGING_CPS,
    SELECTING,
    BUILDING_BEZIER_CP_TANGENT,
    BUILDING_ELLIPSE,
    BULDING_ELLIPSE_CENTER,
    BUILDING_RECTANGLE,
    DRAGGING_LEFT_TANGENT,
    DRAGGING_RIGHT_TANGENT,
    DRAGGING_FEATHER_BAR
};
    
}

struct RotoGui::RotoGuiPrivate
{
    
    RotoGui* publicInterface;
    
    NodeGui* node;
    ViewerGL* viewer;
    
    boost::shared_ptr<RotoContext> context;
    
    Roto_Type type;
    
    QToolBar* toolbar;
    
    QWidget* selectionButtonsBar;
    QHBoxLayout* selectionButtonsBarLayout;
    Button* autoKeyingEnabled;
    Button* featherLinkEnabled;
    Button* stickySelectionEnabled;
    Button* rippleEditEnabled;
    Button* addKeyframeButton;
    Button* removeKeyframeButton;
    
    
    RotoToolButton* selectTool;
    RotoToolButton* pointsEditionTool;
    RotoToolButton* bezierEditionTool;
    
    QAction* selectAllAction;
    
    Roto_Tool selectedTool;
    QToolButton* selectedRole;
    
    SelectedBeziers selectedBeziers;
    
    SelectedCPs selectedCps;
    
    QRectF selectedCpsBbox;
    bool showCpsBbox;
    
    Natron::KeyboardModifiers modifiers;
    
    EventState state;
    
    QPointF lastClickPos;
    QPointF lastMousePos;
    
    QRectF selectionRectangle;
    
    
    boost::shared_ptr<Bezier> builtBezier; //< the bezier currently being built
    
    boost::shared_ptr<BezierCP> tangentBeingDragged; //< the control point whose tangent is being dragged.
                                                     //only relevant when the state is DRAGGING_X_TANGENT
    SelectedCP featherBarBeingDragged;
    
    bool evaluateOnPenUp; //< if true the next pen up will call context->evaluateChange()
    bool evaluateOnKeyUp ; //< if true the next key up will call context->evaluateChange()
    
    RotoGuiPrivate(RotoGui* pub,NodeGui* n,ViewerTab* tab)
    : publicInterface(pub)
    , node(n)
    , viewer(tab->getViewer())
    , context()
    , type(ROTOSCOPING)
    , toolbar(0)
    , selectionButtonsBar(0)
    , selectTool(0)
    , pointsEditionTool(0)
    , bezierEditionTool(0)
    , selectAllAction(0)
    , selectedTool(SELECT_ALL)
    , selectedRole(0)
    , selectedBeziers()
    , selectedCps()
    , selectedCpsBbox()
    , showCpsBbox(false)
    , modifiers(Natron::NoModifier)
    , state(NONE)
    , lastClickPos()
    , selectionRectangle()
    , builtBezier()
    , tangentBeingDragged()
    , featherBarBeingDragged()
    , evaluateOnPenUp(false)
    , evaluateOnKeyUp(false)
    {
        if (n->getNode()->isRotoPaintingNode()) {
            type = ROTOPAINTING;
        }
        context = node->getNode()->getRotoContext();
        assert(context);
    }
    
    void clearSelection();
    
    void clearCPSSelection();
    
    void clearBeziersSelection();
    
    void onCurveLockedChangedRecursive(const boost::shared_ptr<RotoItem>& item,bool* ret);
    
    bool removeBezierFromSelection(const Bezier* b);
    
    void refreshSelectionRectangle(const QPointF& pos);
    
    void drawSelectionRectangle();
    
    void computeSelectedCpsBBOX();
    
    void drawSelectedCpsBBOX();
    
    bool isNearbySelectedCpsCrossHair(const QPointF& pos) const;
    
    void handleBezierSelection(const boost::shared_ptr<Bezier>& curve);
    
    void handleControlPointSelection(const std::pair<boost::shared_ptr<BezierCP>,boost::shared_ptr<BezierCP> >& p);
    
    void drawSelectedCp(int time,const boost::shared_ptr<BezierCP>& cp,double x,double y);
    
    std::pair<boost::shared_ptr<BezierCP>,boost::shared_ptr<BezierCP> >
    isNearbyFeatherBar(int time,const std::pair<double,double>& pixelScale,const QPointF& pos) const;
    
    void dragFeatherPoint(int time,double dx,double dy);
};

RotoToolButton::RotoToolButton(QWidget* parent)
: QToolButton(parent)
{
    
}

void RotoToolButton::mousePressEvent(QMouseEvent* /*event*/)
{
    
}

void RotoToolButton::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        handleSelection();
    } else if (event->button() == Qt::RightButton) {
        showMenu();
    } else {
        QToolButton::mousePressEvent(event);
    }
}

void RotoToolButton::handleSelection()
{
    QAction* curAction = defaultAction();
    if (!isDown()) {
        emit triggered(curAction);
    } else {
        QList<QAction*> allAction = actions();
        for (int i = 0; i < allAction.size(); ++i) {
            if (allAction[i] == curAction) {
                int next = (i == (allAction.size() - 1)) ? 0 : i+1;
                setDefaultAction(allAction[next]);
                emit triggered(allAction[next]);
                break;
            }
        }
    }
}

QAction* RotoGui::createToolAction(QToolButton* toolGroup,
                                   const QIcon& icon,
                                   const QString& text,
                                   const QKeySequence& shortcut,
                                   RotoGui::Roto_Tool tool)
{
    
#pragma message WARN("Change constructor when icons will be added")
    QAction *action = new QAction(icon,text,toolGroup);
    action->setToolTip(text + "<p><b>Keyboard shortcut:" + shortcut.toString(QKeySequence::NativeText) + "</b></p>");
    
    QPoint data;
    data.setX((int)tool);
    if (toolGroup == _imp->selectTool) {
        data.setY((int)SELECTION_ROLE);
    } else if (toolGroup == _imp->pointsEditionTool) {
        data.setY((int)POINTS_EDITION_ROLE);
    } else if (toolGroup == _imp->bezierEditionTool) {
        data.setY(BEZIER_EDITION_ROLE);
    }
    action->setData(QVariant(data));
    QObject::connect(action, SIGNAL(triggered()), this, SLOT(onToolActionTriggered()));
    toolGroup->addAction(action);
    return action;
}

RotoGui::RotoGui(NodeGui* node,ViewerTab* parent)
: _imp(new RotoGuiPrivate(this,node,parent))
{
    assert(parent);
    
    _imp->toolbar = new QToolBar(parent);
    _imp->toolbar->setOrientation(Qt::Vertical);
    _imp->selectionButtonsBar = new QWidget(parent);
    _imp->selectionButtonsBarLayout = new QHBoxLayout(_imp->selectionButtonsBar);
    
    _imp->autoKeyingEnabled = new Button(QIcon(),"Auto-key",_imp->selectionButtonsBar);
    _imp->autoKeyingEnabled->setCheckable(true);
    _imp->autoKeyingEnabled->setChecked(_imp->context->isAutoKeyingEnabled());
    _imp->autoKeyingEnabled->setDown(_imp->context->isAutoKeyingEnabled());
    QObject::connect(_imp->autoKeyingEnabled, SIGNAL(clicked(bool)), this, SLOT(onAutoKeyingButtonClicked(bool)));
    _imp->selectionButtonsBarLayout->addWidget(_imp->autoKeyingEnabled);
    
    _imp->featherLinkEnabled = new Button(QIcon(),"Feather-link",_imp->selectionButtonsBar);
    _imp->featherLinkEnabled->setCheckable(true);
    _imp->featherLinkEnabled->setChecked(_imp->context->isFeatherLinkEnabled());
    _imp->featherLinkEnabled->setDown(_imp->context->isFeatherLinkEnabled());
    QObject::connect(_imp->featherLinkEnabled, SIGNAL(clicked(bool)), this, SLOT(onFeatherLinkButtonClicked(bool)));
    _imp->selectionButtonsBarLayout->addWidget(_imp->featherLinkEnabled);
    
    _imp->stickySelectionEnabled = new Button(QIcon(),"Sticky-selection",_imp->selectionButtonsBar);
    _imp->stickySelectionEnabled->setCheckable(true);
    _imp->stickySelectionEnabled->setChecked(false);
    _imp->stickySelectionEnabled->setDown(false);
    QObject::connect(_imp->stickySelectionEnabled, SIGNAL(clicked(bool)), this, SLOT(onStickySelectionButtonClicked(bool)));
    _imp->selectionButtonsBarLayout->addWidget(_imp->stickySelectionEnabled);
    
    _imp->rippleEditEnabled = new Button(QIcon(),"Ripple-edit",_imp->selectionButtonsBar);
    _imp->rippleEditEnabled->setCheckable(true);
    _imp->rippleEditEnabled->setChecked(_imp->context->isRippleEditEnabled());
    _imp->rippleEditEnabled->setDown(_imp->context->isRippleEditEnabled());
    QObject::connect(_imp->rippleEditEnabled, SIGNAL(clicked(bool)), this, SLOT(onRippleEditButtonClicked(bool)));
    _imp->selectionButtonsBarLayout->addWidget(_imp->rippleEditEnabled);
    
    _imp->addKeyframeButton = new Button(QIcon(),"+ keyframe",_imp->selectionButtonsBar);
    QObject::connect(_imp->addKeyframeButton, SIGNAL(clicked(bool)), this, SLOT(onAddKeyFrameClicked()));
    _imp->selectionButtonsBarLayout->addWidget(_imp->addKeyframeButton);
    
    _imp->removeKeyframeButton = new Button(QIcon(),"- keyframe",_imp->selectionButtonsBar);
    QObject::connect(_imp->removeKeyframeButton, SIGNAL(clicked(bool)), this, SLOT(onRemoveKeyFrameClicked()));
    _imp->selectionButtonsBarLayout->addWidget(_imp->removeKeyframeButton);
        
    _imp->selectTool = new RotoToolButton(_imp->toolbar);
    _imp->selectTool->setPopupMode(QToolButton::InstantPopup);
    QObject::connect(_imp->selectTool, SIGNAL(triggered(QAction*)), this, SLOT(onToolActionTriggered(QAction*)));
    QKeySequence selectShortCut(Qt::Key_Q);
    _imp->selectAllAction = createToolAction(_imp->selectTool, QIcon(), "Select all",selectShortCut, SELECT_ALL);
    createToolAction(_imp->selectTool, QIcon(), "Select points",selectShortCut, SELECT_POINTS);
    createToolAction(_imp->selectTool, QIcon(), "Select curves", selectShortCut,SELECT_CURVES);
    createToolAction(_imp->selectTool, QIcon(), "Select feather points", selectShortCut,SELECT_FEATHER_POINTS);
    _imp->selectTool->setDown(false);
    _imp->selectTool->setDefaultAction(_imp->selectAllAction);
    _imp->toolbar->addWidget(_imp->selectTool);
    
    _imp->pointsEditionTool = new RotoToolButton(_imp->toolbar);
    _imp->pointsEditionTool->setPopupMode(QToolButton::InstantPopup);
    QObject::connect(_imp->pointsEditionTool, SIGNAL(triggered(QAction*)), this, SLOT(onToolActionTriggered(QAction*)));
    _imp->pointsEditionTool->setText("Add points");
    QKeySequence pointsEditionShortcut(Qt::Key_D);
    QAction* addPtsAct = createToolAction(_imp->pointsEditionTool, QIcon(), "Add points",pointsEditionShortcut, ADD_POINTS);
    createToolAction(_imp->pointsEditionTool, QIcon(), "Remove points", pointsEditionShortcut,REMOVE_POINTS);
    createToolAction(_imp->pointsEditionTool, QIcon(), "Cusp points", pointsEditionShortcut,CUSP_POINTS);
    createToolAction(_imp->pointsEditionTool, QIcon(), "Smooth points", pointsEditionShortcut,SMOOTH_POINTS);
    createToolAction(_imp->pointsEditionTool, QIcon(), "Open/Close curve", pointsEditionShortcut,OPEN_CLOSE_CURVE);
    createToolAction(_imp->pointsEditionTool, QIcon(), "Remove feather", pointsEditionShortcut,REMOVE_FEATHER_POINTS);
    _imp->pointsEditionTool->setDown(false);
    _imp->pointsEditionTool->setDefaultAction(addPtsAct);
    _imp->toolbar->addWidget(_imp->pointsEditionTool);
    
    _imp->bezierEditionTool = new RotoToolButton(_imp->toolbar);
    _imp->bezierEditionTool->setPopupMode(QToolButton::InstantPopup);
    QObject::connect(_imp->bezierEditionTool, SIGNAL(triggered(QAction*)), this, SLOT(onToolActionTriggered(QAction*)));
    _imp->bezierEditionTool->setText("Bezier");
    QKeySequence editBezierShortcut(Qt::Key_V);
    QAction* drawBezierAct = createToolAction(_imp->bezierEditionTool, QIcon(), "Bezier",editBezierShortcut, DRAW_BEZIER);
    
    ////B-splines are not implemented yet
    //createToolAction(_imp->bezierEditionTool, QIcon(), "B-Spline", DRAW_B_SPLINE);
    
    createToolAction(_imp->bezierEditionTool, QIcon(), "Ellipse",editBezierShortcut, DRAW_ELLIPSE);
    createToolAction(_imp->bezierEditionTool, QIcon(), "Rectangle", editBezierShortcut,DRAW_RECTANGLE);
    _imp->toolbar->addWidget(_imp->bezierEditionTool);
    
    ////////////Default action is to make a new bezier
    _imp->selectedRole = _imp->selectTool;
    onToolActionTriggered(drawBezierAct);

    QObject::connect(_imp->node->getNode()->getApp()->getTimeLine().get(), SIGNAL(frameChanged(SequenceTime,int)),
                     this, SLOT(onCurrentFrameChanged(SequenceTime,int)));
    QObject::connect(_imp->context.get(), SIGNAL(refreshViewerOverlays()), this, SLOT(onRefreshAsked()));
    QObject::connect(_imp->context.get(), SIGNAL(selectionChanged(int)), this, SLOT(onSelectionChanged(int)));
    QObject::connect(_imp->context.get(), SIGNAL(itemLockedChanged()), this, SLOT(onCurveLockedChanged()));
    restoreSelectionFromContext();
}

RotoGui::~RotoGui()
{
    
} 

QWidget* RotoGui::getButtonsBar(RotoGui::Roto_Role role) const
{
    switch (role) {
        case SELECTION_ROLE:
            return _imp->selectionButtonsBar;
            break;
        case POINTS_EDITION_ROLE:
            return _imp->selectionButtonsBar;
            break;
        case BEZIER_EDITION_ROLE:
            return _imp->selectionButtonsBar;
            break;
        default:
            assert(false);
            break;
    }
}

QWidget* RotoGui::getCurrentButtonsBar() const
{
    return getButtonsBar(getCurrentRole());
}

RotoGui::Roto_Tool RotoGui::getSelectedTool() const
{
    return _imp->selectedTool;
}

QToolBar* RotoGui::getToolBar() const
{
    return _imp->toolbar;
}

void RotoGui::onToolActionTriggered()
{
    QAction* act = qobject_cast<QAction*>(sender());
    if (act) {
        onToolActionTriggered(act);
    }
}

void RotoGui::onToolActionTriggered(QAction* act)
{
    QPoint data = act->data().toPoint();
    Roto_Role actionRole = (Roto_Role)data.y();
    QToolButton* toolButton = 0;
    
    Roto_Role previousRole = getCurrentRole();
    
    switch (actionRole) {
        case SELECTION_ROLE:
            toolButton = _imp->selectTool;
            emit roleChanged((int)previousRole,(int)SELECTION_ROLE);
            break;
        case POINTS_EDITION_ROLE:
            toolButton = _imp->pointsEditionTool;
            emit roleChanged((int)previousRole,(int)POINTS_EDITION_ROLE);
            break;
        case BEZIER_EDITION_ROLE:
            toolButton = _imp->bezierEditionTool;
            emit roleChanged((int)previousRole,(int)BEZIER_EDITION_ROLE);
            break;
        default:
            assert(false);
            break;
    }
    
    
    assert(_imp->selectedRole);
    if (_imp->selectedRole != toolButton) {
        _imp->selectedRole->setDown(false);
    }
    
    ///reset the selected control points
    _imp->selectedCps.clear();
    _imp->showCpsBbox = false;
    _imp->selectedCpsBbox.setTopLeft(QPointF(0,0));
    _imp->selectedCpsBbox.setTopRight(QPointF(0,0));
    
    ///clear all selection if we were building a new bezier
    if (previousRole == BEZIER_EDITION_ROLE && _imp->selectedTool == DRAW_BEZIER && _imp->builtBezier) {
        _imp->builtBezier->setCurveFinished(true);
        _imp->clearSelection();
    }
    
    
    assert(toolButton);
    toolButton->setDown(true);
    toolButton->setDefaultAction(act);
    _imp->selectedRole = toolButton;
    _imp->selectedTool = (Roto_Tool)data.x();
}

RotoGui::Roto_Role RotoGui::getCurrentRole() const
{
    if (_imp->selectedRole == _imp->selectTool) {
        return SELECTION_ROLE;
    } else if (_imp->selectedRole == _imp->pointsEditionTool) {
        return POINTS_EDITION_ROLE;
    } else if (_imp->selectedRole == _imp->bezierEditionTool) {
        return BEZIER_EDITION_ROLE;
    }
    assert(false);
}

void RotoGui::RotoGuiPrivate::drawSelectedCp(int time,const boost::shared_ptr<BezierCP>& cp,double x,double y)
{
    ///if the tangent is being dragged, color it
    bool colorLeftTangent = false;
    bool colorRightTangent = false;
    if (cp == tangentBeingDragged &&
        (state == DRAGGING_LEFT_TANGENT  || state == DRAGGING_RIGHT_TANGENT)) {
        colorLeftTangent = state == DRAGGING_LEFT_TANGENT ? true : false;
        colorRightTangent = !colorLeftTangent;
    }
    
    
    double leftDerivX,leftDerivY,rightDerivX,rightDerivY;
    cp->getLeftBezierPointAtTime(time, &leftDerivX, &leftDerivY);
    cp->getRightBezierPointAtTime(time, &rightDerivX, &rightDerivY);
    
    bool drawLeftHandle = leftDerivX != x || leftDerivY != y;
    bool drawRightHandle = rightDerivX != x || rightDerivY != y;
    glBegin(GL_POINTS);
    if (drawLeftHandle) {
        if (colorLeftTangent) {
            glColor3f(0.2, 1., 0.);
        }
        glVertex2d(leftDerivX,leftDerivY);
        if (colorLeftTangent) {
            glColor3d(0.85, 0.67, 0.);
        }
    }
    if (drawRightHandle) {
        if (colorRightTangent) {
            glColor3f(0.2, 1., 0.);
        }
        glVertex2d(rightDerivX,rightDerivY);
        if (colorRightTangent) {
            glColor3d(0.85, 0.67, 0.);
        }
    }
    glEnd();
    
    glBegin(GL_LINE_STRIP);
    if (drawLeftHandle) {
        glVertex2d(leftDerivX,leftDerivY);
    }
    glVertex2d(x, y);
    if (drawRightHandle) {
        glVertex2d(rightDerivX,rightDerivY);
    }
    glEnd();

}

void RotoGui::drawOverlays(double /*scaleX*/,double /*scaleY*/) const
{
    std::list< boost::shared_ptr<Bezier> > beziers = _imp->context->getCurvesByRenderOrder();
    int time = _imp->context->getTimelineCurrentTime();
    
    std::pair<double,double> pixelScale;
    std::pair<double,double> viewportSize;
    
    _imp->viewer->getPixelScale(pixelScale.first, pixelScale.second);
    _imp->viewer->getViewportSize(viewportSize.first, viewportSize.second);
    
    glPushAttrib(GL_HINT_BIT | GL_ENABLE_BIT | GL_LINE_BIT | GL_COLOR_BUFFER_BIT | GL_POINT_BIT);
    glEnable(GL_LINE_SMOOTH);
    glEnable(GL_BLEND);
    glHint(GL_LINE_SMOOTH_HINT,GL_DONT_CARE);
    glLineWidth(1.5);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glPointSize(7.);
    glEnable(GL_POINT_SMOOTH);
    for (std::list< boost::shared_ptr<Bezier> >::const_iterator it = beziers.begin(); it!=beziers.end(); ++it) {
        
        if ((*it)->isActivated(time)) {
            
            ///draw the bezier
            std::list< Point > points;
            (*it)->evaluateAtTime_DeCastelJau(time,0, 100, &points);
            
            double curveColor[4];
            if (!(*it)->isLockedRecursive()) {
                (*it)->getOverlayColor(curveColor);
            } else {
                curveColor[0] = 0.8;curveColor[1] = 0.8;curveColor[2] = 0.8;curveColor[3] = 1.;
            }
            glColor4dv(curveColor);
            
            glBegin(GL_LINE_STRIP);
            for (std::list<Point >::const_iterator it2 = points.begin(); it2!=points.end(); ++it2) {
                glVertex2f(it2->x, it2->y);
            }
            glEnd();
            
            ///draw the feather points
            std::list< Point > featherPoints;
            RectD featherBBox(INT_MAX,INT_MAX,INT_MIN,INT_MIN);
            (*it)->evaluateFeatherPointsAtTime_DeCastelJau(time,0, 100, &featherPoints,true,&featherBBox);
            std::vector<double> constants(featherPoints.size()),multiples(featherPoints.size());
            Bezier::precomputePointInPolygonTables(featherPoints, &constants, &multiples);
            
            if (!featherPoints.empty()) {
                glLineStipple(2, 0xAAAA);
                glEnable(GL_LINE_STIPPLE);
                glBegin(GL_LINE_STRIP);
                for (std::list<Point >::const_iterator it2 = featherPoints.begin(); it2!=featherPoints.end(); ++it2) {
                    glVertex2f(it2->x, it2->y);
                }
                glEnd();
                glDisable(GL_LINE_STIPPLE);
            }
            
            ///draw the control points if the bezier is selected
            std::list< boost::shared_ptr<Bezier> >::const_iterator selected =
            std::find(_imp->selectedBeziers.begin(),_imp->selectedBeziers.end(),*it);
            
            if (selected != _imp->selectedBeziers.end()) {
                const std::list< boost::shared_ptr<BezierCP> >& cps = (*selected)->getControlPoints();
                const std::list< boost::shared_ptr<BezierCP> >& featherPts = (*selected)->getFeatherPoints();
                assert(cps.size() == featherPts.size());
                
                double cpHalfWidth = kControlPointMidSize * pixelScale.first;
                double cpHalfHeight = kControlPointMidSize * pixelScale.second;
                
                glColor3d(0.85, 0.67, 0.);
                
                std::list< boost::shared_ptr<BezierCP> >::const_iterator itF = featherPts.begin();
                int index = 0;
                
                std::list< boost::shared_ptr<BezierCP> >::const_iterator prevCp = cps.end();
                --prevCp;
                std::list< boost::shared_ptr<BezierCP> >::const_iterator nextCp = cps.begin();
                ++nextCp;
                for (std::list< boost::shared_ptr<BezierCP> >::const_iterator it2 = cps.begin(); it2!=cps.end();
                     ++it2,++itF,++index,++nextCp,++prevCp) {
                    
                    if (nextCp == cps.end()) {
                        nextCp = cps.begin();
                    }
                    if (prevCp == cps.end()) {
                        prevCp = cps.begin();
                    }
                    
                    double x,y;
                    (*it2)->getPositionAtTime(time, &x, &y);
                    
                    ///if the control point is the only control point being dragged, color it to identify it to the user
                    bool colorChanged = false;
                    SelectedCPs::const_iterator firstSelectedCP = _imp->selectedCps.begin();
                    if ((firstSelectedCP->first == *it2)
                        && _imp->selectedCps.size() == 1 && _imp->state == DRAGGING_CPS) {
                        glColor3f(0.2, 1., 0.);
                        colorChanged = true;
                    }
                    
                    glBegin(GL_POLYGON);
                    glVertex2f(x - cpHalfWidth, y - cpHalfHeight);
                    glVertex2f(x + cpHalfWidth, y - cpHalfHeight);
                    glVertex2f(x + cpHalfWidth, y + cpHalfHeight);
                    glVertex2f(x - cpHalfWidth, y + cpHalfHeight);
                    glEnd();
                    
                    if (colorChanged) {
                        glColor3d(0.85, 0.67, 0.);
                    }
                    
                    if ((firstSelectedCP->first == *itF)
                        && _imp->selectedCps.size() == 1 && _imp->state == DRAGGING_CPS && !colorChanged) {
                        glColor3f(0.2, 1., 0.);
                        colorChanged = true;
                    }
                    
                    double xF,yF;
                    (*itF)->getPositionAtTime(time, &xF, &yF);
                    ///draw the feather point only if it is distinct from the associated point
                    bool drawFeather = !(*it2)->equalsAtTime(time, **itF);
                    double distFeatherX = 20. * pixelScale.first;
                    double distFeatherY = 20. * pixelScale.second;
                    if (drawFeather) {
                        glBegin(GL_POLYGON);
                        glVertex2f(xF - cpHalfWidth, yF - cpHalfHeight);
                        glVertex2f(xF + cpHalfWidth, yF - cpHalfHeight);
                        glVertex2f(xF + cpHalfWidth, yF + cpHalfHeight);
                        glVertex2f(xF - cpHalfWidth, yF + cpHalfHeight);
                        glEnd();
                        
                        if (_imp->state == DRAGGING_FEATHER_BAR &&
                            (*itF == _imp->featherBarBeingDragged.first || *itF == _imp->featherBarBeingDragged.second)) {
                            glColor3f(0.2, 1., 0.);
                            colorChanged = true;
                        } else {
                           
                            glColor4dv(curveColor);
                        }
                        
                        double beyondX,beyondY;
                        double dx = (xF - x);
                        double dy = (yF - y);
                        double dist = sqrt(dx * dx + dy * dy);
                        beyondX = (dx * (dist + distFeatherX)) / dist + x;
                        beyondY = (dy * (dist + distFeatherY)) / dist + y;
                        
                        ///draw a link between the feather point and the control point.
                        ///Also extend that link of 20 pixels beyond the feather point.
                        
                        glBegin(GL_LINE_STRIP);
                        glVertex2f(x, y);
                        glVertex2f(xF, yF);
                        glVertex2f(beyondX, beyondY);
                        glEnd();
                        
                    } else {
                        ///if the feather point is identical to the control point
                        ///draw a small hint line that the user can drag to move the feather point
                        if (_imp->selectedTool == SELECT_ALL || _imp->selectedTool == SELECT_FEATHER_POINTS) {
                            int cpCount = (*it2)->getCurve()->getControlPointsCount();
                            if (cpCount > 1) {
                                
                                Natron::Point controlPoint;
                                controlPoint.x = x;
                                controlPoint.y = y;
                                Natron::Point featherPoint;
                                featherPoint.x = xF;
                                featherPoint.y = yF;
                                
                                Bezier::expandToFeatherDistance(controlPoint, &featherPoint, distFeatherX, featherPoints, constants, multiples, featherBBox, time, prevCp, it2, nextCp);
                                
                                if (_imp->state == DRAGGING_FEATHER_BAR &&
                                    (*itF == _imp->featherBarBeingDragged.first || *itF == _imp->featherBarBeingDragged.second)) {
                                    glColor3f(0.2, 1., 0.);
                                    colorChanged = true;
                                } else {
                                    glColor4dv(curveColor);
                                }
                              
                                glBegin(GL_LINES);
                                glVertex2f(x, y);
                                glVertex2f(featherPoint.x, featherPoint.y);
                                glEnd();
                                
                                glColor3d(0.85, 0.67, 0.);
                      
                            }
                        }
                    }
                    
                    
                    if (colorChanged) {
                        glColor3d(0.85, 0.67, 0.);
                        colorChanged = false;
                    }

                    
                    for (SelectedCPs::const_iterator cpIt = _imp->selectedCps.begin();cpIt != _imp->selectedCps.end();++cpIt) {
                        
                        ///if the control point is selected, draw its tangent handles
                        if (cpIt->first == *it2) {
                            _imp->drawSelectedCp(time, cpIt->first, x, y);
                            if (drawFeather) {
                                _imp->drawSelectedCp(time, cpIt->second, xF, yF);
                            }
                        } else if (cpIt->second == *it2) {
                            _imp->drawSelectedCp(time, cpIt->second, x, y);
                            if (drawFeather) {
                                _imp->drawSelectedCp(time, cpIt->first, xF, yF);
                            }
 
                        }
                    }
                   
  
                }
            }
        }
    }
    
    glDisable(GL_LINE_SMOOTH);
    glDisable(GL_POINT_SMOOTH);
    glLineWidth(1.);
    glPointSize(1.);
    glDisable(GL_BLEND);
    glPopAttrib();
    

    if (_imp->state == SELECTING) {
        _imp->drawSelectionRectangle();
    }
    
    if (_imp->showCpsBbox && _imp->state != SELECTING) {
        _imp->drawSelectedCpsBBOX();
    }
}

void RotoGui::RotoGuiPrivate::drawSelectedCpsBBOX()
{
    std::pair<double,double> pixelScale;
    viewer->getPixelScale(pixelScale.first, pixelScale.second);
    
    glPushAttrib(GL_HINT_BIT | GL_ENABLE_BIT | GL_LINE_BIT | GL_COLOR_BUFFER_BIT);
    glEnable(GL_LINE_SMOOTH);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glHint(GL_LINE_SMOOTH_HINT,GL_DONT_CARE);
    
    
    QPointF topLeft = selectedCpsBbox.topLeft();
    QPointF btmRight = selectedCpsBbox.bottomRight();
    
    glLineWidth(1.5);
    
    glColor4f(0.8,0.8,0.8,1.);
    glBegin(GL_LINE_STRIP);
    glVertex2f(topLeft.x(),btmRight.y());
    glVertex2f(topLeft.x(),topLeft.y());
    glVertex2f(btmRight.x(),topLeft.y());
    glVertex2f(btmRight.x(),btmRight.y());
    glVertex2f(topLeft.x(),btmRight.y());
    glEnd();
    
    double midX = (topLeft.x() + btmRight.x()) / 2.;
    double midY = (btmRight.y() + topLeft.y()) / 2.;
    
    double xHairMidSizeX = kXHairSelectedCpsBox * pixelScale.first;
    double xHairMidSizeY = kXHairSelectedCpsBox * pixelScale.second;

    
    QLineF selectedCpsCrossHorizLine;
    selectedCpsCrossHorizLine.setLine(midX - xHairMidSizeX, midY, midX + xHairMidSizeX, midY);
    QLineF selectedCpsCrossVertLine;
    selectedCpsCrossVertLine.setLine(midX, midY - xHairMidSizeY, midX, midY + xHairMidSizeY);
    
    glBegin(GL_LINES);
    glVertex2f(std::max(selectedCpsCrossHorizLine.p1().x(),topLeft.x()),selectedCpsCrossHorizLine.p1().y());
    glVertex2f(std::min(selectedCpsCrossHorizLine.p2().x(),btmRight.x()),selectedCpsCrossHorizLine.p2().y());
    glVertex2f(selectedCpsCrossVertLine.p1().x(),std::max(selectedCpsCrossVertLine.p1().y(),btmRight.y()));
    glVertex2f(selectedCpsCrossVertLine.p2().x(),std::min(selectedCpsCrossVertLine.p2().y(),topLeft.y()));
    glEnd();
    
    glDisable(GL_LINE_SMOOTH);
    glCheckError();
    
    glLineWidth(1.);
    glPopAttrib();
    glColor4f(1., 1., 1., 1.);
}

void RotoGui::RotoGuiPrivate::drawSelectionRectangle()
{
    
    glPushAttrib(GL_HINT_BIT | GL_ENABLE_BIT | GL_LINE_BIT | GL_COLOR_BUFFER_BIT);
    glEnable(GL_LINE_SMOOTH);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glHint(GL_LINE_SMOOTH_HINT,GL_DONT_CARE);
    
    glColor4f(0.5,0.8,1.,0.2);
    QPointF btmRight = selectionRectangle.bottomRight();
    QPointF topLeft = selectionRectangle.topLeft();
    
    glBegin(GL_POLYGON);
    glVertex2f(topLeft.x(),btmRight.y());
    glVertex2f(topLeft.x(),topLeft.y());
    glVertex2f(btmRight.x(),topLeft.y());
    glVertex2f(btmRight.x(),btmRight.y());
    glEnd();
    
    
    glLineWidth(1.5);
    
    glBegin(GL_LINE_STRIP);
    glVertex2f(topLeft.x(),btmRight.y());
    glVertex2f(topLeft.x(),topLeft.y());
    glVertex2f(btmRight.x(),topLeft.y());
    glVertex2f(btmRight.x(),btmRight.y());
    glVertex2f(topLeft.x(),btmRight.y());
    glEnd();
    
    
    glDisable(GL_LINE_SMOOTH);
    glCheckError();
    
    glLineWidth(1.);
    glPopAttrib();
    glColor4f(1., 1., 1., 1.);

}

void RotoGui::RotoGuiPrivate::refreshSelectionRectangle(const QPointF& pos)
{
    double xmin = std::min(lastClickPos.x(),pos.x());
    double xmax = std::max(lastClickPos.x(),pos.x());
    double ymin = std::min(lastClickPos.y(),pos.y());
    double ymax = std::max(lastClickPos.y(),pos.y());
    selectionRectangle.setBottomRight(QPointF(xmax,ymin));
    selectionRectangle.setTopLeft(QPointF(xmin,ymax));

    clearSelection();
    
    int selectionMode;
    if (selectedTool == SELECT_ALL) {
        selectionMode = 0;
    } else if (selectedTool == SELECT_POINTS) {
        selectionMode = 1;
    } else if (selectedTool == SELECT_FEATHER_POINTS) {
        selectionMode = 2;
    } else {
        ///this function can only be called if the current selected tool is one of the 3 aforementioned
        assert(false);
    }
    
    std::list<boost::shared_ptr<Bezier> > curves = context->getCurvesByRenderOrder();
    for (std::list<boost::shared_ptr<Bezier> >::const_iterator it = curves.begin(); it!=curves.end(); ++it) {
        
        if (!(*it)->isLockedRecursive()) {
            SelectedCPs points  = (*it)->controlPointsWithinRect(xmin, xmax, ymin, ymax, 0,selectionMode);
            selectedCps.insert(selectedCps.end(), points.begin(), points.end());
            if (!points.empty()) {
                selectedBeziers.push_back(*it);
            }
        }
    }
    context->select(curves, RotoContext::OVERLAY_INTERACT);
    
    computeSelectedCpsBBOX();
}

void RotoGui::RotoGuiPrivate::clearSelection()
{
    clearBeziersSelection();
    clearCPSSelection();
    
}
void RotoGui::RotoGuiPrivate::clearCPSSelection()
{
    selectedCps.clear();
    showCpsBbox = false;
    selectedCpsBbox.setTopLeft(QPointF(0,0));
    selectedCpsBbox.setTopRight(QPointF(0,0));
}

void RotoGui::RotoGuiPrivate::clearBeziersSelection()
{
    context->clearSelection(RotoContext::OVERLAY_INTERACT);
    selectedBeziers.clear();
}

bool RotoGui::RotoGuiPrivate::removeBezierFromSelection(const Bezier* b)
{
    for (SelectedBeziers::iterator fb = selectedBeziers.begin(); fb != selectedBeziers.end(); ++fb) {
        if (fb->get() == b) {
            context->deselect(*fb,RotoContext::OVERLAY_INTERACT);
            selectedBeziers.erase(fb);
            return true;
        }
    }
    return false;
}

static void handleControlPointMaximum(int time,const BezierCP& p,double* l,double *b,double *r,double *t)
{
    double x,y,xLeft,yLeft,xRight,yRight;
    p.getPositionAtTime(time, &x, &y);
    p.getLeftBezierPointAtTime(time, &xLeft, &yLeft);
    p.getRightBezierPointAtTime(time, &xRight, &yRight);
    
    *r = std::max(x, *r);
    *l = std::min(x, *l);
    
    *r = std::max(xLeft, *r);
    *l = std::min(xLeft, *l);
  
    *r = std::max(xRight, *r);
    *l = std::min(xRight, *l);
    
    *t = std::max(y, *t);
    *b = std::min(y, *b);
    
    *t = std::max(yLeft, *t);
    *b = std::min(yLeft, *b);

    
    *t = std::max(yRight, *t);
    *b = std::min(yRight, *b);
}

void RotoGui::RotoGuiPrivate::computeSelectedCpsBBOX()
{
    int time = context->getTimelineCurrentTime();
    std::pair<double, double> pixelScale;
    viewer->getPixelScale(pixelScale.first,pixelScale.second);
    
    
    double l = INT_MAX,r = INT_MIN,b = INT_MAX,t = INT_MIN;
    for (SelectedCPs::iterator it = selectedCps.begin(); it!=selectedCps.end(); ++it) {
        handleControlPointMaximum(time,*(it->first),&l,&b,&r,&t);
        handleControlPointMaximum(time,*(it->second),&l,&b,&r,&t);
    }
    selectedCpsBbox.setCoords(l, t, r, b);
    if (selectedCps.size() > 1) {
        showCpsBbox = true;
    } else {
        showCpsBbox = false;
    }
}

void RotoGui::RotoGuiPrivate::handleBezierSelection(const boost::shared_ptr<Bezier>& curve)
{
    ///find out if the bezier is already selected.
    SelectedBeziers::const_iterator found =
    std::find(selectedBeziers.begin(),selectedBeziers.end(),curve);
    
    if (found == selectedBeziers.end()) {
        
        ///clear previous selection if the SHIFT modifier isn't held
        if (!modifiers.testFlag(Natron::ShiftModifier)) {
            clearBeziersSelection();
        }
        selectedBeziers.push_back(curve);
        context->select(curve,RotoContext::OVERLAY_INTERACT);
    }

}

void RotoGui::RotoGuiPrivate::handleControlPointSelection(const std::pair<boost::shared_ptr<BezierCP>,boost::shared_ptr<BezierCP> >& p)
{
    ///find out if the cp is already selected.
    SelectedCPs::const_iterator foundCP = selectedCps.end();
    for (SelectedCPs::const_iterator it = selectedCps.begin(); it!=selectedCps.end(); ++it) {
        if (p.first == it->first) {
            foundCP = it;
            break;
        }
    }
    
    if (foundCP == selectedCps.end()) {
        ///clear previous selection if the SHIFT modifier isn't held
        if (!modifiers.testFlag(Natron::ShiftModifier)) {
            selectedCps.clear();
        }
        selectedCps.push_back(p);
        computeSelectedCpsBBOX();
  
    }
    
    state = DRAGGING_CPS;

}

namespace {

static void dragTangent(int time,BezierCP& p,double dx,double dy,bool left,bool autoKeying)
{
    double leftX,leftY,rightX,rightY,x,y;
    bool isOnKeyframe = p.getLeftBezierPointAtTime(time, &leftX, &leftY);
    p.getRightBezierPointAtTime(time, &rightX, &rightY);
    p.getPositionAtTime(time, &x, &y);
    double dist = left ?  sqrt((rightX - x) * (rightX - x) + (rightY - y) * (rightY - y))
    : sqrt((leftX - x) * (leftX - x) + (leftY - y) * (leftY - y));
    if (left) {
        leftX += dx;
        leftY += dy;
    } else {
        rightX += dx;
        rightY += dy;
    }
    double alpha = left ? std::atan2(y - leftY,x - leftX) : std::atan2(y - rightY,x - rightX);
    
    if (left) {
        rightX = std::cos(alpha) * dist;
        rightY = std::sin(alpha) * dist;
        if (autoKeying || isOnKeyframe) {
            p.getCurve()->setPointLeftAndRightIndex(p, time, leftX, leftY, x + rightX, y + rightY);
        }
    } else {
        leftX = std::cos(alpha) * dist;
        leftY = std::sin(alpha) * dist;
        if (autoKeying || isOnKeyframe) {
            p.getCurve()->setPointLeftAndRightIndex(p, time, x + leftX , y + leftY , rightX , rightY);
        }
    }
    
}

}


bool RotoGui::penDown(double /*scaleX*/,double /*scaleY*/,const QPointF& /*viewportPos*/,const QPointF& pos)
{
    std::pair<double, double> pixelScale;
    _imp->viewer->getPixelScale(pixelScale.first, pixelScale.second);
    
    bool didSomething = false;

    int time = _imp->context->getTimelineCurrentTime();
    
    ////////////////// TANGENT SELECTION
    ///in all cases except cusp/smooth if a control point is selected, check if the user clicked on a tangent handle
    ///in which case we go into DRAGGING_TANGENT mode
    int tangentSelectionTol = kTangentHandleSelectionTolerance * pixelScale.first;
    if (_imp->selectedTool != CUSP_POINTS && _imp->selectedTool != SMOOTH_POINTS && _imp->selectedTool != SELECT_CURVES) {
        for (SelectedCPs::iterator it = _imp->selectedCps.begin(); it!=_imp->selectedCps.end(); ++it) {
            if (_imp->selectedTool == SELECT_ALL) {
                int ret = it->first->isNearbyTangent(time, pos.x(), pos.y(), tangentSelectionTol);
                if (ret >= 0) {
                    _imp->tangentBeingDragged = it->first;
                    _imp->state = ret == 0 ? DRAGGING_LEFT_TANGENT : DRAGGING_RIGHT_TANGENT;
                    didSomething = true;
                } else {
                    ///try with the counter part point
                    ret = it->second->isNearbyTangent(time, pos.x(), pos.y(), tangentSelectionTol);
                    if (ret >= 0) {
                        _imp->tangentBeingDragged = it->second;
                        _imp->state = ret == 0 ? DRAGGING_LEFT_TANGENT : DRAGGING_RIGHT_TANGENT;
                        didSomething = true;
                    }
                }
            } else if (_imp->selectedTool == SELECT_FEATHER_POINTS) {
                const boost::shared_ptr<BezierCP>& fp = it->first->isFeatherPoint() ? it->first : it->second;
                int ret = fp->isNearbyTangent(time, pos.x(), pos.y(), tangentSelectionTol);
                if (ret >= 0) {
                    _imp->tangentBeingDragged = fp;
                    _imp->state = ret == 0 ? DRAGGING_LEFT_TANGENT : DRAGGING_RIGHT_TANGENT;
                    didSomething = true;
                }
            } else if (_imp->selectedTool == SELECT_POINTS) {
                const boost::shared_ptr<BezierCP>& cp = it->first->isFeatherPoint() ? it->second : it->first;
                int ret = cp->isNearbyTangent(time, pos.x(), pos.y(), tangentSelectionTol);
                if (ret >= 0) {
                    _imp->tangentBeingDragged = cp;
                    _imp->state = ret == 0 ? DRAGGING_LEFT_TANGENT : DRAGGING_RIGHT_TANGENT;
                    didSomething = true;
                }
            }
            
            if (didSomething) {
                return didSomething;
            }
        }
    }
    
    //////////////////BEZIER SELECTION
    /////Check if the point is nearby a bezier
    ///tolerance for bezier selection
    double bezierSelectionTolerance = kBezierSelectionTolerance * pixelScale.first;
    double nearbyBezierT;
    int nearbyBezierCPIndex;
    bool isFeather;
    boost::shared_ptr<Bezier> nearbyBezier =
    _imp->context->isNearbyBezier(pos.x(), pos.y(), bezierSelectionTolerance,&nearbyBezierCPIndex,&nearbyBezierT,&isFeather);

    std::pair<boost::shared_ptr<BezierCP>,boost::shared_ptr<BezierCP> > nearbyCP;
    int nearbyCpIndex = -1;
    double cpSelectionTolerance = kControlPointSelectionTolerance * pixelScale.first;
    if (nearbyBezier) {
        /////////////////CONTROL POINT SELECTION
        //////Check if the point is nearby a control point of a selected bezier
        ///Find out if the user selected a control point
        if (nearbyBezier->isLockedRecursive()) {
            nearbyBezier.reset();
        } else {
            Bezier::ControlPointSelectionPref pref = Bezier::WHATEVER_FIRST;
            if (_imp->selectedTool == SELECT_FEATHER_POINTS) {
                pref = Bezier::FEATHER_FIRST;
            }
            
            nearbyCP = nearbyBezier->isNearbyControlPoint(pos.x(), pos.y(), cpSelectionTolerance,pref,&nearbyCpIndex);
        }

    }
    switch (_imp->selectedTool) {
        case SELECT_ALL:
        case SELECT_POINTS:
        case SELECT_FEATHER_POINTS:
        {
            
            std::pair<boost::shared_ptr<BezierCP>,boost::shared_ptr<BezierCP> > featherBarSel;
            if (_imp->selectedTool == SELECT_ALL || _imp->selectedTool == SELECT_FEATHER_POINTS) {
                featherBarSel = _imp->isNearbyFeatherBar(time, pixelScale, pos);
            }
            if (nearbyBezier) {
                _imp->handleBezierSelection(nearbyBezier);
                ///check if the user clicked nearby the cross hair of the selection rectangle in which case
                ///we drag all the control points selected
                if (_imp->isNearbySelectedCpsCrossHair(pos)) {
                    _imp->state = DRAGGING_CPS;
                } else if (nearbyCP.first) {
                    _imp->handleControlPointSelection(nearbyCP);
                }  else if (featherBarSel.first) {
                    _imp->featherBarBeingDragged = featherBarSel;
                    _imp->state = DRAGGING_FEATHER_BAR;
                }
                
            } else {
                
                if (featherBarSel.first) {
                    _imp->featherBarBeingDragged = featherBarSel;
                    _imp->state = DRAGGING_FEATHER_BAR;
                } else if (_imp->isNearbySelectedCpsCrossHair(pos)) {
                    ///check if the user clicked nearby the cross hair of the selection rectangle in which case
                    ///we drag all the control points selected
                    _imp->state = DRAGGING_CPS;
                } else {
                    if (!isStickySelectionEnabled() && !_imp->modifiers.testFlag(Natron::ShiftModifier)) {
                        _imp->clearSelection();
                        _imp->selectionRectangle.setTopLeft(pos);
                        _imp->selectionRectangle.setBottomRight(pos);
                        _imp->state = SELECTING;
                        
                    }
                }
            }
            didSomething = true;
            
        }   break;
        case SELECT_CURVES:
            if (nearbyBezier) {
                _imp->handleBezierSelection(nearbyBezier);
            } else {
                if (!isStickySelectionEnabled() && !_imp->modifiers.testFlag(Natron::ShiftModifier)) {
                    _imp->clearSelection();
                    _imp->selectionRectangle.setTopLeft(pos);
                    _imp->selectionRectangle.setBottomRight(pos);
                    _imp->state = SELECTING;
                    
                }
            }
            break;
        case ADD_POINTS:
#pragma message WARN("Make this an undo/redo command")
            ///If the user clicked on a bezier and this bezier is selected add a control point by
            ///splitting up the targeted segment
            if (nearbyBezier) {
                SelectedBeziers::const_iterator foundBezier =
                std::find(_imp->selectedBeziers.begin(), _imp->selectedBeziers.end(), nearbyBezier);
                if (foundBezier != _imp->selectedBeziers.end()) {
                    ///check that the point is not too close to an existing point
                    if (nearbyCP.first) {
                        _imp->handleControlPointSelection(nearbyCP);
                    } else {
                        boost::shared_ptr<BezierCP> newCp = nearbyBezier->addControlPointAfterIndex(nearbyBezierCPIndex, nearbyBezierT);
                        boost::shared_ptr<BezierCP> newFp = nearbyBezier->getFeatherPointAtIndex(nearbyBezierCPIndex + 1);
                        _imp->handleControlPointSelection(std::make_pair(newCp,newFp));
                        _imp->evaluateOnPenUp = true;
                    }
                    didSomething = true;
                }
            }
            break;
        case REMOVE_POINTS:
#pragma message WARN("Make this an undo/redo command")
            if (nearbyCP.first) {
                Bezier* curve = nearbyCP.first->getCurve();
                assert(nearbyBezier.get() == curve);
                if (nearbyCP.first->isFeatherPoint()) {
                    curve->removeControlPointByIndex(curve->getControlPointIndex(nearbyCP.second));
                } else {
                    curve->removeControlPointByIndex(curve->getControlPointIndex(nearbyCP.first));
                }
                int cpCount = curve->getControlPointsCount();
                if (cpCount == 1) {
                    curve->setCurveFinished(false);
                } else if (cpCount == 0) {
                    
                    ///clear the shared pointer so the bezier gets deleted
                    _imp->context->removeItem(nearbyBezier.get());

                }
                SelectedCPs::iterator foundSelected = std::find(_imp->selectedCps.begin(), _imp->selectedCps.end(), nearbyCP);
                if (foundSelected != _imp->selectedCps.end()) {
                    _imp->selectedCps.erase(foundSelected);
                }
                _imp->computeSelectedCpsBBOX();
                _imp->evaluateOnPenUp = true;
                didSomething = true;
            }
            break;
        case REMOVE_FEATHER_POINTS:
#pragma message WARN("Make this an undo/redo command")
            ///clear control points selections
            _imp->selectedCps.clear();
            _imp->showCpsBbox = false;
            if (nearbyCP.first) {
                assert(nearbyBezier);
                _imp->handleControlPointSelection(nearbyCP);
                nearbyBezier->removeFeatherAtIndex(nearbyCpIndex);
                _imp->computeSelectedCpsBBOX();
                _imp->evaluateOnPenUp = true;
                didSomething = true;
            }
            break;
        case OPEN_CLOSE_CURVE:
            if (nearbyBezier) {
#pragma message WARN("Make this an undo/redo command")
                SelectedBeziers::iterator foundBezier =
                std::find(_imp->selectedBeziers.begin(), _imp->selectedBeziers.end(), nearbyBezier);
                if (foundBezier != _imp->selectedBeziers.end()) {
                    nearbyBezier->setCurveFinished(!nearbyBezier->isCurveFinished());
                    _imp->evaluateOnPenUp = true;
                    didSomething = true;
                } else {
                    _imp->handleBezierSelection(nearbyBezier);
                }
            }
            break;
        case SMOOTH_POINTS:
#pragma message WARN("Make this an undo/redo command")
            ///clear control points selections
            _imp->selectedCps.clear();
            _imp->showCpsBbox = false;
            if (nearbyCP.first) {
                assert(nearbyBezier);
                _imp->handleControlPointSelection(nearbyCP);
                nearbyBezier->smoothPointAtIndex(nearbyCpIndex, time);
                _imp->evaluateOnPenUp = true;
                _imp->computeSelectedCpsBBOX();
                didSomething = true;
            }
            break;
        case CUSP_POINTS:
#pragma message WARN("Make this an undo/redo command")
            ///clear control points selections
            _imp->selectedCps.clear();
            _imp->showCpsBbox = false;
            if (nearbyCP.first && _imp->context->isAutoKeyingEnabled()) {
                _imp->handleControlPointSelection(nearbyCP);
                assert(nearbyBezier);
                nearbyBezier->cuspPointAtIndex(nearbyCpIndex, time);
                _imp->computeSelectedCpsBBOX();
                _imp->evaluateOnPenUp = true;
                didSomething = true;
            }
            break;
        case DRAW_BEZIER:
        {
#pragma message WARN("Make this an undo/redo command")
            ///make a new curve
            if (!_imp->builtBezier) {
                
                boost::shared_ptr<Bezier> newCurve = _imp->context->makeBezier(pos.x(), pos.y(),kRotoBezierBaseName);
                boost::shared_ptr<BezierCP> cp = newCurve->getControlPointAtIndex(0);
                boost::shared_ptr<BezierCP> fp = newCurve->getFeatherPointAtIndex(0);
                assert(cp && fp);
                _imp->selectedCps.push_back(std::make_pair(cp,fp));
                _imp->builtBezier = newCurve;
            } else {
                
                _imp->handleBezierSelection(_imp->builtBezier);
                
                
                ///if the user clicked on a control point of the bezier, select the point instead.
                ///if that point is the starting point of the curve, close the curve
                const std::list<boost::shared_ptr<BezierCP> >& cps = _imp->builtBezier->getControlPoints();
                int i = 0;
                for (std::list<boost::shared_ptr<BezierCP> >::const_iterator it = cps.begin(); it!=cps.end(); ++it,++i) {
                    double x,y;
                    (*it)->getPositionAtTime(time, &x, &y);
                    if (x >= (pos.x() - cpSelectionTolerance) && x <= (pos.x() + cpSelectionTolerance) &&
                        y >= (pos.y() - cpSelectionTolerance) && y <= (pos.y() + cpSelectionTolerance)) {
                        if (it == cps.begin()) {
                            _imp->builtBezier->setCurveFinished(true);
                            _imp->evaluateOnPenUp = true;
        
                            _imp->builtBezier.reset();
                            
                            _imp->selectedCps.clear();
                            onToolActionTriggered(_imp->selectAllAction);
                            
                            
                        } else {
                            boost::shared_ptr<BezierCP> fp = _imp->builtBezier->getFeatherPointAtIndex(i);
                            assert(fp);
                            _imp->handleControlPointSelection(std::make_pair(*it, fp));
                        }
                        
                        return true;
                    }
                }
                
                ///continue the curve being built
                _imp->builtBezier->addControlPoint(pos.x(), pos.y());
                int lastIndex = _imp->builtBezier->getControlPointsCount() - 1;
                assert(lastIndex > 0);
                boost::shared_ptr<BezierCP> cp = _imp->builtBezier->getControlPointAtIndex(lastIndex);
                boost::shared_ptr<BezierCP> fp = _imp->builtBezier->getFeatherPointAtIndex(lastIndex);
                assert(cp && fp);
                _imp->selectedCps.push_back(std::make_pair(cp,fp));
            }
            _imp->evaluateOnPenUp = true;
            _imp->state = BUILDING_BEZIER_CP_TANGENT;
            didSomething = true;
        }   break;
        case DRAW_B_SPLINE:
            
            break;
        case DRAW_ELLIPSE:
        {
#pragma message WARN("Make this an undo/redo command")
            _imp->builtBezier = _imp->context->makeBezier(pos.x(), pos.y(),kRotoEllipseBaseName);
            _imp->builtBezier->getControlPointAtIndex(0);
            _imp->builtBezier->addControlPoint(pos.x(), pos.y());
            _imp->builtBezier->addControlPoint(pos.x(), pos.y());
            _imp->builtBezier->addControlPoint(pos.x(), pos.y());
            _imp->builtBezier->setCurveFinished(true);
            _imp->evaluateOnPenUp = true;
            _imp->handleBezierSelection(_imp->builtBezier);
            if (_imp->modifiers.testFlag(Natron::ControlModifier)) {
                _imp->state = BULDING_ELLIPSE_CENTER;
            } else {
                _imp->state = BUILDING_ELLIPSE;
            }
            didSomething = true;
            
        }   break;
        case DRAW_RECTANGLE:
        {
            boost::shared_ptr<Bezier> curve = _imp->context->makeBezier(pos.x(), pos.y(),kRotoRectangleBaseName);
            curve->addControlPoint(pos.x(), pos.y());
            curve->addControlPoint(pos.x(), pos.y());
            curve->addControlPoint(pos.x(), pos.y());
            curve->setCurveFinished(true);
            _imp->evaluateOnPenUp = true;
            _imp->handleBezierSelection(curve);
            _imp->state = BUILDING_RECTANGLE;
            didSomething = true;
        }   break;
        default:
            assert(false);
            break;
    }
    
    _imp->lastClickPos = pos;
    _imp->lastMousePos = pos;
    return didSomething;
}

bool RotoGui::penMotion(double /*scaleX*/,double /*scaleY*/,const QPointF& /*viewportPos*/,const QPointF& pos)
{
    std::pair<double, double> pixelScale;
    _imp->viewer->getPixelScale(pixelScale.first, pixelScale.second);
    
    int time = _imp->context->getTimelineCurrentTime();
    ///Set the cursor to the appropriate case
    bool cursorSet = false;
    if (_imp->selectedCps.size() > 1 && _imp->isNearbySelectedCpsCrossHair(pos)) {
        _imp->viewer->setCursor(QCursor(Qt::SizeAllCursor));
        cursorSet = true;
    } else {
        double cpTol = kControlPointSelectionTolerance * pixelScale.first;
        
        if (_imp->state != DRAGGING_CPS) {
            for (SelectedBeziers::const_iterator it = _imp->selectedBeziers.begin(); it!=_imp->selectedBeziers.end(); ++it) {
                int index = -1;
                std::pair<boost::shared_ptr<BezierCP>,boost::shared_ptr<BezierCP> > nb =
                (*it)->isNearbyControlPoint(pos.x(), pos.y(), cpTol,Bezier::WHATEVER_FIRST,&index);
                if (index != -1) {
                    _imp->viewer->setCursor(QCursor(Qt::CrossCursor));
                    cursorSet = true;
                    break;
                }
            }
        }
        if (!cursorSet && _imp->state != DRAGGING_LEFT_TANGENT && _imp->state != DRAGGING_RIGHT_TANGENT) {
            ///find a nearby tangent
            for (SelectedCPs::const_iterator it = _imp->selectedCps.begin(); it!=_imp->selectedCps.end(); ++it) {
                if (it->first->isNearbyTangent(time, pos.x(), pos.y(), cpTol) != -1) {
                    _imp->viewer->setCursor(QCursor(Qt::CrossCursor));
                    cursorSet = true;
                    break;
                }
            }
        }
    }
    if (!cursorSet) {
        _imp->viewer->setCursor(QCursor(Qt::ArrowCursor));
    }
    
    
    double dx = pos.x() - _imp->lastMousePos.x();
    double dy = pos.y() - _imp->lastMousePos.y();
    bool didSomething = false;
    switch (_imp->state) {
        case DRAGGING_CPS:
#pragma message WARN("Make this a mergeable undo/redo command")
        {
            for (SelectedCPs::iterator it = _imp->selectedCps.begin(); it!=_imp->selectedCps.end(); ++it) {
                int index;
                if (it->first->isFeatherPoint()) {
                    if (_imp->selectedTool == SELECT_FEATHER_POINTS || _imp->selectedTool == SELECT_ALL) {
                        index = it->second->getCurve()->getControlPointIndex(it->second);
                        assert(index != -1);
                        it->first->getCurve()->moveFeatherByIndex(index,time, dx, dy);
                    }
                } else {
                    if (_imp->selectedTool == SELECT_POINTS || _imp->selectedTool == SELECT_ALL) {
                        index = it->first->getCurve()->getControlPointIndex(it->first);
                        assert(index != -1);
                        it->first->getCurve()->movePointByIndex(index,time, dx, dy);
                    }
                }
            }
            _imp->evaluateOnPenUp = true;
            _imp->computeSelectedCpsBBOX();
            didSomething = true;
        }   break;
        case SELECTING:
        {
            _imp->refreshSelectionRectangle(pos);
            didSomething = true;
        }   break;
        case BUILDING_BEZIER_CP_TANGENT:
        {
#pragma message WARN("Make this a mergeable undo/redo command")
            assert(_imp->builtBezier);
            int lastIndex = _imp->builtBezier->getControlPointsCount() - 1;
            assert(lastIndex >= 0);
            _imp->builtBezier->moveLeftBezierPoint(lastIndex ,time, -dx, -dy);
            _imp->builtBezier->moveRightBezierPoint(lastIndex, time, dx, dy);
            ///no need to set _imp->evaluateOnPenUp = true because the polygon is not closed anyway
            didSomething = true;
        }   break;
        case BUILDING_ELLIPSE:
        {
#pragma message WARN("Make this a mergeable undo/redo command")
            assert(_imp->builtBezier);
            
            boost::shared_ptr<BezierCP> top = _imp->builtBezier->getControlPointAtIndex(0);
            boost::shared_ptr<BezierCP> right = _imp->builtBezier->getControlPointAtIndex(1);
            boost::shared_ptr<BezierCP> bottom = _imp->builtBezier->getControlPointAtIndex(2);
            boost::shared_ptr<BezierCP> left = _imp->builtBezier->getControlPointAtIndex(3);
            
            //top only moves by x
            _imp->builtBezier->movePointByIndex(0,time, dx / 2., 0);
            
            //right
            _imp->builtBezier->movePointByIndex(1,time, dx, dy / 2.);
            
            //bottom
            _imp->builtBezier->movePointByIndex(2,time, dx / 2., dy );
            
            //left only moves by y
            _imp->builtBezier->movePointByIndex(3,time, 0, dy / 2.);
            
            double topX,topY,rightX,rightY,btmX,btmY,leftX,leftY;
            top->getPositionAtTime(time, &topX, &topY);
            right->getPositionAtTime(time, &rightX, &rightY);
            bottom->getPositionAtTime(time, &btmX, &btmY);
            left->getPositionAtTime(time, &leftX, &leftY);
            
            _imp->builtBezier->setLeftBezierPoint(0, time,  (leftX + topX) / 2., topY);
            _imp->builtBezier->setRightBezierPoint(0, time, (rightX + topX) / 2., topY);
            
            _imp->builtBezier->setLeftBezierPoint(1, time,  rightX, (rightY + topY) / 2.);
            _imp->builtBezier->setRightBezierPoint(1, time, rightX, (rightY + btmY) / 2.);
            
            _imp->builtBezier->setLeftBezierPoint(2, time,  (rightX + btmX) / 2., btmY);
            _imp->builtBezier->setRightBezierPoint(2, time, (leftX + btmX) / 2., btmY);
            
            _imp->builtBezier->setLeftBezierPoint(3, time,   leftX, (btmY + leftY) / 2.);
            _imp->builtBezier->setRightBezierPoint(3, time, leftX, (topY + leftY) / 2.);
            

            didSomething = true;
            _imp->evaluateOnPenUp = true;
        }   break;
        case BULDING_ELLIPSE_CENTER:
        {
#pragma message WARN("Make this a mergeable undo/redo command")
            assert(_imp->builtBezier);
            
            boost::shared_ptr<BezierCP> top = _imp->builtBezier->getControlPointAtIndex(0);
            boost::shared_ptr<BezierCP> right = _imp->builtBezier->getControlPointAtIndex(1);
            boost::shared_ptr<BezierCP> bottom = _imp->builtBezier->getControlPointAtIndex(2);
            boost::shared_ptr<BezierCP> left = _imp->builtBezier->getControlPointAtIndex(3);
            
            //top only moves by x
            _imp->builtBezier->movePointByIndex(0,time, 0, dy);
            
            //right
            _imp->builtBezier->movePointByIndex(1,time, dx , 0);
            
            //bottom
            _imp->builtBezier->movePointByIndex(2,time, 0., -dy );
            
            //left only moves by y
            _imp->builtBezier->movePointByIndex(3,time, -dx, 0);
            double topX,topY,rightX,rightY,btmX,btmY,leftX,leftY;
            top->getPositionAtTime(time, &topX, &topY);
            right->getPositionAtTime(time, &rightX, &rightY);
            bottom->getPositionAtTime(time, &btmX, &btmY);
            left->getPositionAtTime(time, &leftX, &leftY);
            
            _imp->builtBezier->setLeftBezierPoint(0, time,  (leftX + topX) / 2., topY);
            _imp->builtBezier->setRightBezierPoint(0, time, (rightX + topX) / 2., topY);
            
            _imp->builtBezier->setLeftBezierPoint(1, time,  rightX, (rightY + topY) / 2.);
            _imp->builtBezier->setRightBezierPoint(1, time, rightX, (rightY + btmY) / 2.);
            
            _imp->builtBezier->setLeftBezierPoint(2, time,  (rightX + btmX) / 2., btmY);
            _imp->builtBezier->setRightBezierPoint(2, time, (leftX + btmX) / 2., btmY);
            
            _imp->builtBezier->setLeftBezierPoint(3, time,   leftX, (btmY + leftY) / 2.);
            _imp->builtBezier->setRightBezierPoint(3, time, leftX, (topY + leftY) / 2.);
            didSomething = true;
        }   break;
        case BUILDING_RECTANGLE:
        {
#pragma message WARN("Make this a mergeable undo/redo command")
            assert(_imp->selectedBeziers.size() == 1);
            boost::shared_ptr<Bezier>& curve = _imp->selectedBeziers.front();
            curve->movePointByIndex(1,time, dx, 0);
            curve->movePointByIndex(2,time, dx, dy);
            curve->movePointByIndex(3,time, 0, dy);
            didSomething = true;
            _imp->evaluateOnPenUp = true;
        }   break;
        case DRAGGING_LEFT_TANGENT:
        {
#pragma message WARN("Make this a mergeable undo/redo command")
            assert(_imp->tangentBeingDragged);
            boost::shared_ptr<BezierCP> counterPart;
            if (_imp->tangentBeingDragged->isFeatherPoint()) {
                counterPart = _imp->tangentBeingDragged->getCurve()->getControlPointForFeatherPoint(_imp->tangentBeingDragged);
            } else {
                counterPart = _imp->tangentBeingDragged->getCurve()->getFeatherPointForControlPoint(_imp->tangentBeingDragged);
            }
            assert(counterPart);
            bool autoKeying = _imp->context->isAutoKeyingEnabled();
            dragTangent(time, *_imp->tangentBeingDragged, dx, dy, true,autoKeying);
            dragTangent(time, *counterPart, dx, dy, true,autoKeying);
            _imp->computeSelectedCpsBBOX();
            _imp->evaluateOnPenUp = true;
            didSomething = true;
        }   break;
        case DRAGGING_RIGHT_TANGENT:
        {
#pragma message WARN("Make this a mergeable undo/redo command")
            assert(_imp->tangentBeingDragged);
            boost::shared_ptr<BezierCP> counterPart;
            if (_imp->tangentBeingDragged->isFeatherPoint()) {
                counterPart = _imp->tangentBeingDragged->getCurve()->getControlPointForFeatherPoint(_imp->tangentBeingDragged);
            } else {
                counterPart = _imp->tangentBeingDragged->getCurve()->getFeatherPointForControlPoint(_imp->tangentBeingDragged);
            }
            assert(counterPart);
            bool autoKeying = _imp->context->isAutoKeyingEnabled();
            dragTangent(time, *_imp->tangentBeingDragged, dx, dy, false,autoKeying);
            dragTangent(time, *counterPart, dx, dy, false,autoKeying);
            _imp->computeSelectedCpsBBOX();
            _imp->evaluateOnPenUp = true;
            didSomething = true;
        }   break;
        case DRAGGING_FEATHER_BAR:
        {
#pragma message WARN("Make this a mergeable undo/redo command")
            ///drag the feather point targeted of the euclidean distance of dx,dy in the direction perpendicular to
            ///the derivative of the curve at the point
            _imp->dragFeatherPoint(time, dx, dy);
            _imp->evaluateOnPenUp = true;
            didSomething = true;
        }   break;
        case NONE:
            
            break;
        default:
            break;
    }
    _imp->lastMousePos = pos;
    return didSomething;
}

bool RotoGui::penUp(double /*scaleX*/,double /*scaleY*/,const QPointF& /*viewportPos*/,const QPointF& /*pos*/)
{
    if (_imp->evaluateOnPenUp) {
        _imp->context->evaluateChange();
        _imp->node->getNode()->getApp()->triggerAutoSave();
        _imp->evaluateOnPenUp = false;
    }
    _imp->tangentBeingDragged.reset();
    _imp->featherBarBeingDragged.first.reset();
    _imp->featherBarBeingDragged.second.reset();
    _imp->state = NONE;
    
    if (_imp->selectedTool == DRAW_ELLIPSE || _imp->selectedTool == DRAW_RECTANGLE) {
        _imp->selectedCps.clear();
        onToolActionTriggered(_imp->selectAllAction);
    }
    
    return true;
}

bool RotoGui::keyDown(double /*scaleX*/,double /*scaleY*/,QKeyEvent* e)
{
    bool didSomething = false;
    _imp->modifiers = QtEnumConvert::fromQtModifiers(e->modifiers());
    if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
#pragma message WARN("Make this an  undo/redo command")
        ///if control points are selected, delete them, otherwise delete the selected beziers
        if (!_imp->selectedCps.empty()) {
            std::list < Bezier* > beziersToRemove;
            for (SelectedCPs::iterator it = _imp->selectedCps.begin(); it != _imp->selectedCps.end(); ++it) {
                Bezier* curve = it->first->getCurve();
                if (it->first->isFeatherPoint()) {
                    curve->removeControlPointByIndex(curve->getControlPointIndex(it->second));
                } else {
                    curve->removeControlPointByIndex(curve->getControlPointIndex(it->first));
                }
                int cpCount = curve->getControlPointsCount();
                if (cpCount == 1) {
                    curve->setCurveFinished(false);
                } else if (cpCount == 0) {
                    beziersToRemove.push_back(curve);
                    if (curve == _imp->builtBezier.get()) {
                        _imp->builtBezier.reset();
                    }
                }
            }
            for (std::list < Bezier* > ::iterator it = beziersToRemove.begin(); it != beziersToRemove.end(); ++it) {
                ///clear the shared pointer so the bezier gets deleted
                _imp->context->removeItem(*it);
            }
            
            _imp->selectedCps.clear();
            _imp->computeSelectedCpsBBOX();
            _imp->node->getNode()->getApp()->triggerAutoSave();
            _imp->context->evaluateChange();
            didSomething = true;
        } else if (!_imp->selectedBeziers.empty()) {
            while (!_imp->selectedBeziers.empty()) {
                Bezier* b = _imp->selectedBeziers.front().get();
                _imp->context->removeItem(b);
                if (b == _imp->builtBezier.get()) {
                    _imp->builtBezier.reset();
                }
            }
            _imp->node->getNode()->getApp()->triggerAutoSave();
            _imp->context->evaluateChange();
            didSomething = true;
        }
        
    } else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
#pragma message WARN("Make this an  undo/redo command")                            
        if (_imp->selectedTool == DRAW_BEZIER && _imp->builtBezier && !_imp->builtBezier->isCurveFinished()) {
            _imp->builtBezier->setCurveFinished(true);
            _imp->builtBezier.reset();
            _imp->selectedCps.clear();
            onToolActionTriggered(_imp->selectAllAction);
            _imp->node->getNode()->getApp()->triggerAutoSave();
            _imp->context->evaluateChange();
            didSomething = true;
        }
    } else if (e->key() == Qt::Key_A && e->modifiers().testFlag(Qt::ControlModifier)) {
        ///if no bezier are selected, select all beziers
        if (_imp->selectedBeziers.empty()) {
            std::list<boost::shared_ptr<Bezier> > bez = _imp->context->getCurvesByRenderOrder();
            for (std::list<boost::shared_ptr<Bezier> >::const_iterator it = bez.begin(); it!=bez.end(); ++it) {
                _imp->context->select(*it,RotoContext::OVERLAY_INTERACT);
                _imp->selectedBeziers.push_back(*it);
            }
        } else {
            ///select all the control points of all selected beziers
            _imp->selectedCps.clear();
            for (SelectedBeziers::iterator it = _imp->selectedBeziers.begin(); it!=_imp->selectedBeziers.end(); ++it) {
                const std::list<boost::shared_ptr<BezierCP> >& cps = (*it)->getControlPoints();
                const std::list<boost::shared_ptr<BezierCP> >& fps = (*it)->getFeatherPoints();
                assert(cps.size() == fps.size());
                
                std::list<boost::shared_ptr<BezierCP> >::const_iterator cpIT = cps.begin();
                for (std::list<boost::shared_ptr<BezierCP> >::const_iterator fpIT = fps.begin(); fpIT != fps.end(); ++fpIT, ++ cpIT) {
                    _imp->selectedCps.push_back(std::make_pair(*cpIT, *fpIT));
                }
            }
            _imp->computeSelectedCpsBBOX();
        }
        didSomething = true;
    } else if (e->key() == Qt::Key_Q) {
        _imp->selectTool->handleSelection();
    } else if (e->key() == Qt::Key_V) {
        _imp->bezierEditionTool->handleSelection();
    } else if (e->key() == Qt::Key_D) {
        _imp->pointsEditionTool->handleSelection();
    }
    
    return didSomething;
}

bool RotoGui::keyUp(double /*scaleX*/,double /*scaleY*/,QKeyEvent* e)
{
    _imp->modifiers = QtEnumConvert::fromQtModifiers(e->modifiers());
    if (_imp->evaluateOnKeyUp) {
        _imp->context->evaluateChange();
        _imp->node->getNode()->getApp()->triggerAutoSave();
        _imp->evaluateOnKeyUp = false;
    }
    return false;
}

bool RotoGui::RotoGuiPrivate::isNearbySelectedCpsCrossHair(const QPointF& pos) const
{
    
    std::pair<double, double> pixelScale;
    viewer->getPixelScale(pixelScale.first,pixelScale.second);

    double xHairMidSizeX = kXHairSelectedCpsBox * pixelScale.first;
    double xHairMidSizeY = kXHairSelectedCpsBox * pixelScale.second;
    
    double l = selectedCpsBbox.topLeft().x();
    double r = selectedCpsBbox.bottomRight().x();
    double b = selectedCpsBbox.bottomRight().y();
    double t = selectedCpsBbox.topLeft().y();
    
    double toleranceX = kXHairSelectedCpsTolerance * pixelScale.first;
    double toleranceY = kXHairSelectedCpsTolerance * pixelScale.second;

    double midX = (l + r) / 2.;
    double midY = (b + t) / 2.;
    
    double lCross = midX - xHairMidSizeX;
    double rCross = midX + xHairMidSizeX;
    double bCross = midY - xHairMidSizeY;
    double tCross = midY + xHairMidSizeY;
    
    if (pos.x() >= (lCross - toleranceX) &&
        pos.x() <= (rCross + toleranceX) &&
        pos.y() <= (tCross + toleranceY) &&
        pos.y() >= (bCross - toleranceY)) {
        return true;
    } else {
        return false;
    }
}

namespace {
struct ExpandFeatherData
{
    std::vector<double> constants,multipliers;
    std::list<Point> polygon;
    RectD bbox;
};
}

std::pair<boost::shared_ptr<BezierCP>,boost::shared_ptr<BezierCP> >
RotoGui::RotoGuiPrivate::isNearbyFeatherBar(int time,const std::pair<double,double>& pixelScale,const QPointF& pos) const
{
    double distFeatherX = 20. * pixelScale.first;

    double acceptance = 6. * pixelScale.second;
    
    std::map<Bezier*, ExpandFeatherData* > beziersData;
    
    for (SelectedCPs::const_iterator it = selectedCps.begin(); it!=selectedCps.end(); ++it) {
        boost::shared_ptr<BezierCP> p = it->first->isFeatherPoint() ? it->second : it->first;
        boost::shared_ptr<BezierCP> fp = it->first->isFeatherPoint() ? it->first : it->second;
        
        std::map<Bezier*, ExpandFeatherData* >::iterator foundData = beziersData.find(p->getCurve());
        
        const std::list<boost::shared_ptr<BezierCP> >& cps = p->getCurve()->getControlPoints();
        int cpCount = (int)cps.size();
        if (cpCount <= 1) {
            continue;
        }
        
        Point controlPoint,featherPoint;
        p->getPositionAtTime(time, &controlPoint.x, &controlPoint.y);
        fp->getPositionAtTime(time, &featherPoint.x, &featherPoint.y);
        
        ExpandFeatherData* bezierData = 0;
        if (foundData != beziersData.end()) {
            bezierData = foundData->second;
        } else {
            bezierData = new ExpandFeatherData;
            bezierData->bbox.set(INT_MAX,INT_MAX,INT_MIN,INT_MIN);
            p->getCurve()->evaluateFeatherPointsAtTime_DeCastelJau(time, 0, 50, &bezierData->polygon, true,&bezierData->bbox);
            bezierData->constants.resize(bezierData->polygon.size());
            bezierData->multipliers.resize(bezierData->polygon.size());
            Bezier::precomputePointInPolygonTables(bezierData->polygon, &bezierData->constants, &bezierData->multipliers);
            beziersData.insert(std::make_pair(p->getCurve(), bezierData));
        }
        
        std::list<boost::shared_ptr<BezierCP> >::const_iterator prev = cps.end();
        --prev;
        std::list<boost::shared_ptr<BezierCP> >::const_iterator next = cps.begin();
        ++next;
        std::list<boost::shared_ptr<BezierCP> >::const_iterator cur = cps.begin();
        for (; cur!=cps.end(); ++cur,++prev,++next) {
            if (prev == cps.end()) {
                prev = cps.begin();
            }
            if (next == cps.end()) {
                next = cps.begin();
            }
            
            if (*cur == p) {
                break;
            }
        }
        
        Bezier::expandToFeatherDistance(controlPoint, &featherPoint, distFeatherX, bezierData->polygon, bezierData->constants, bezierData->multipliers, bezierData->bbox, time, prev, cur, next);
        
        if (featherPoint.x == controlPoint.x) {
            ///vertical line
            if (pos.y() >= (controlPoint.y - acceptance) && pos.y() <= (featherPoint.y + acceptance) &&
                pos.x() >= (controlPoint.x - acceptance) && pos.x() <= (controlPoint.x + acceptance)) {
                return *it;
            }
        } else {
            double a = (featherPoint.y - controlPoint.y) / (featherPoint.x - controlPoint.x);
            double b =  controlPoint.y - a * controlPoint.x;
            if (std::abs(pos.y() - (a * pos.x() + b)) < acceptance) {
                return *it;
            }
        }
        
    }
    for (std::map<Bezier*, ExpandFeatherData* >::iterator it = beziersData.begin();it!=beziersData.end();++it) {
        delete it->second;
    }
    return std::make_pair(boost::shared_ptr<BezierCP>(), boost::shared_ptr<BezierCP>());
}

void RotoGui::RotoGuiPrivate::dragFeatherPoint(int time,double dx,double dy)
{
    assert(featherBarBeingDragged.first && featherBarBeingDragged.second);
    
    double alphaDrag;
    double dragDistance;
    if (dx != 0.) {
        alphaDrag = std::atan(dy / dx);
        dragDistance = dx / std::cos(alphaDrag);
    } else {
        alphaDrag = dy < 0. ? - pi / 2. : pi / 2.;
        dragDistance = dy;
    }
   
    
    boost::shared_ptr<BezierCP> p = featherBarBeingDragged.first->isFeatherPoint() ?
    featherBarBeingDragged.second : featherBarBeingDragged.first;
    boost::shared_ptr<BezierCP> fp = featherBarBeingDragged.first->isFeatherPoint() ?
    featherBarBeingDragged.first : featherBarBeingDragged.second;
    
    Point featherPoint,controlPoint;
    p->getPositionAtTime(time, &controlPoint.x, &controlPoint.y);
    bool isOnKeyframe = fp->getPositionAtTime(time, &featherPoint.x, &featherPoint.y);
    
    
    ///the feather point equals the control point, use derivatives
    const std::list<boost::shared_ptr<BezierCP> >& cps = p->getCurve()->getControlPoints();
    assert(cps.size() > 1);
    
    std::list<boost::shared_ptr<BezierCP> >::const_iterator prev = cps.end();
    --prev;
    std::list<boost::shared_ptr<BezierCP> >::const_iterator next = cps.begin();
    ++next;
    std::list<boost::shared_ptr<BezierCP> >::const_iterator cur = cps.begin();
    for (; cur!=cps.end(); ++cur,++prev,++next) {
        if (prev == cps.end()) {
            prev = cps.begin();
        }
        if (next == cps.end()) {
            next = cps.begin();
        }
        
        if (*cur == p) {
            break;
        }
    }

    std::list<Point> featherPolygon;
    RectD featherBBox;
    p->getCurve()->evaluateFeatherPointsAtTime_DeCastelJau(time, 0, 50 , &featherPolygon, true,&featherBBox);
    std::vector<double> constants(featherPolygon.size()),multipliers(featherPolygon.size());
    Bezier::precomputePointInPolygonTables(featherPolygon, &constants, &multipliers);
    Point delta = Bezier::expandToFeatherDistance(controlPoint, &featherPoint,dragDistance, featherPolygon, constants, multipliers, featherBBox, time, prev, cur, next);
    
    if (context->isAutoKeyingEnabled() || isOnKeyframe) {
        int index = fp->getCurve()->getFeatherPointIndex(fp);
        double leftX,leftY,rightX,rightY;
        
        fp->getLeftBezierPointAtTime(time, &leftX, &leftY);
        fp->getRightBezierPointAtTime(time, &rightX, &rightY);

        fp->getCurve()->setPointAtIndex(true, index, time, featherPoint.x,featherPoint.y,
                                        leftX + delta.x, leftY + delta.y,
                                        rightX + delta.x, rightY + delta.y);
        
    }
    
}

void RotoGui::onAutoKeyingButtonClicked(bool e)
{
    _imp->autoKeyingEnabled->setDown(e);
    _imp->context->onAutoKeyingChanged(e);
}

void RotoGui::onFeatherLinkButtonClicked(bool e)
{
    _imp->featherLinkEnabled->setDown(e);
    _imp->context->onFeatherLinkChanged(e);
}

void RotoGui::onRippleEditButtonClicked(bool e)
{
    _imp->rippleEditEnabled->setDown(e);
    _imp->context->onRippleEditChanged(e);
}

void RotoGui::onStickySelectionButtonClicked(bool e)
{
    _imp->stickySelectionEnabled->setDown(e);
}

bool RotoGui::isStickySelectionEnabled() const
{
    return _imp->stickySelectionEnabled->isChecked();
}

void RotoGui::onAddKeyFrameClicked()
{
    int time = _imp->context->getTimelineCurrentTime();
    for (SelectedBeziers::iterator it = _imp->selectedBeziers.begin(); it!=_imp->selectedBeziers.end(); ++it) {
        (*it)->setKeyframe(time);
    }
}

void RotoGui::onRemoveKeyFrameClicked()
{
    int time = _imp->context->getTimelineCurrentTime();
    for (SelectedBeziers::iterator it = _imp->selectedBeziers.begin(); it!=_imp->selectedBeziers.end(); ++it) {
        (*it)->removeKeyframe(time);
    }
}

void RotoGui::onCurrentFrameChanged(SequenceTime /*time*/,int)
{
    _imp->computeSelectedCpsBBOX();
}

void RotoGui::restoreSelectionFromContext()
{
    _imp->selectedBeziers = _imp->context->getSelectedCurves();
}

void RotoGui::onRefreshAsked()
{
    _imp->viewer->redraw();
}

void RotoGui::RotoGuiPrivate::onCurveLockedChangedRecursive(const boost::shared_ptr<RotoItem>& item,bool* ret)
{
    boost::shared_ptr<Bezier> b = boost::dynamic_pointer_cast<Bezier>(item);
    boost::shared_ptr<RotoLayer> layer = boost::dynamic_pointer_cast<RotoLayer>(item);
    if (b) {
        if (item->getLocked()) {
            for (SelectedBeziers::iterator fb = selectedBeziers.begin(); fb != selectedBeziers.end(); ++fb) {
                if (fb->get() == b.get()) {
                    selectedBeziers.erase(fb);
                    *ret = true;
                }
            }
        } else {
            ///Explanation: This change has been made in result to a user click on the settings panel.
            ///We have to reselect the bezier overlay hence put a reason different of OVERLAY_INTERACT
            SelectedBeziers::iterator found = std::find(selectedBeziers.begin(),selectedBeziers.end(),b);
            if (found == selectedBeziers.end()) {
                selectedBeziers.push_back(b);
                context->select(b, RotoContext::SETTINGS_PANEL);
                *ret  = true;
            }
        }
        

    } else if (layer) {
        const std::list<boost::shared_ptr<RotoItem> >& items = layer->getItems();
        for (std::list<boost::shared_ptr<RotoItem> >::const_iterator it = items.begin(); it != items.end(); ++it) {
            onCurveLockedChangedRecursive(*it, ret);
        }
    }
}

void RotoGui::onCurveLockedChanged()
{
    boost::shared_ptr<RotoItem> item = _imp->context->getLastItemLocked();
    assert(item);
    bool changed = false;
    if (item) {
        _imp->onCurveLockedChangedRecursive(item, &changed);
    }
    if (changed) {
        _imp->viewer->redraw();
    }
    
}

void RotoGui::onSelectionChanged(int reason)
{
    if ((RotoContext::SelectionReason)reason != RotoContext::OVERLAY_INTERACT) {
 
        _imp->selectedBeziers = _imp->context->getSelectedCurves();
        _imp->viewer->redraw();
    }
}