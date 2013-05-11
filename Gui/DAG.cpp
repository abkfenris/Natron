#include <QtGui/QGraphicsProxyWidget>
#include "Gui/DAG.h"
#include "Superviser/controler.h"
#include "Gui/arrowGUI.h"
#include "Core/hash.h"
#include "Core/outputnode.h"
#include "Core/inputnode.h"
#include "Core/OP.h"
#include "Gui/DAGQuickNode.h"
#include "Gui/mainGui.h"
#include "Gui/dockableSettings.h"
#include "Core/model.h"
#include "Core/viewerNode.h"
#include "Reader/Reader.h"
#include "Gui/knob.h"
#include "Gui/GLViewer.h"
#include "Gui/viewerTab.h"
#include "Core/VideoEngine.h"

NodeGraph::NodeGraph(QGraphicsScene* scene,QWidget *parent):QGraphicsView(scene,parent),_fullscreen(false){
    setMouseTracking(true);
    setCacheMode(CacheBackground);
    setViewportUpdateMode(BoundingRectViewportUpdate);
    setRenderHint(QPainter::Antialiasing);
    setTransformationAnchor(AnchorUnderMouse);
    scale(qreal(0.8), qreal(0.8));

    setDragMode(QGraphicsView::ScrollHandDrag);
    smartNodeCreationEnabled=true;
}
void NodeGraph::addNode_ui(QVBoxLayout *dockContainer,qreal x, qreal y, UI_NODE_TYPE type, Node *node){
    QGraphicsScene* sc=scene();
    Node_ui* node_ui;

    if(type==OUTPUT){
        node_ui=new OutputNode_ui(ctrl,nodes,dockContainer,(node),x,y,0,sc);
    }else if(type==INPUT_NODE){
        node_ui=new InputNode_ui(ctrl,nodes,dockContainer,(node),x,y,0,sc);
    }else if(type==OPERATOR){
        node_ui=new OperatorNode_ui(ctrl,nodes,dockContainer,(node),x,y,0,sc);
    }
	
    nodes.push_back(node_ui);
    
}
void NodeGraph::mousePressEvent(QMouseEvent *event){
    old_pos=mapToScene(event->pos());
    oldp=event->pos();
    int i=0;
    bool found=false;
    while(i<nodes.size() && !found){
        Node_ui* n=nodes[i];

        QPointF evpt=n->mapFromScene(old_pos);
        if(n->contains(evpt)){

            node_dragged=n;
            state=NODE_DRAGGING;
            found=true;
            break;
        }else{
            std::vector<Arrow*> arrows=n->getInputsArrows();
            int j=0;
            while(j<arrows.size()){
                Arrow* a=arrows[j];

                if(a->contains(evpt)){

                    arrow_dragged=a;
                    state=ARROW_DRAGGING;
                    found=true;
                    break;
                }
                j++;
            }

        }
        i++;
    }
    if(!found){
        state=MOVING_AREA;
       // setCursor(QCursor(Qt::OpenHandCursor));
        QGraphicsView::mousePressEvent(event);

    }

}
void NodeGraph::mouseReleaseEvent(QMouseEvent *event){
    if(state==ARROW_DRAGGING){
        if(arrow_dragged->hasSource()){


            arrow_dragged->getSource()->getNode()->incrementFreeOutputNb();

            arrow_dragged->getSource()->getNode()->removeChild(arrow_dragged->getDest()->getNode());
            arrow_dragged->getSource()->substractChild(arrow_dragged->getDest());

            arrow_dragged->getDest()->getNode()->removeParent(arrow_dragged->getSource()->getNode());
            arrow_dragged->getDest()->substractParent(arrow_dragged->getSource());

            arrow_dragged->removeSource();
            scene()->update();
            if(!strcmp(arrow_dragged->getDest()->getNode()->class_name(),"Viewer")){
                ViewerGL* gl_viewer = ctrl->getGui()->viewer_tab->viewer;
                
                ctrl->getModel()->getVideoEngine()->abort(); // aborting current work
                
                gl_viewer->drawing(false);
                gl_viewer->blankInfoForViewer();
                gl_viewer->initViewer();
                ctrl->getModel()->getVideoEngine()->clearInfos(arrow_dragged->getDest()->getNode());
                gl_viewer->clearViewer();
                std::vector<InputNode*> inp;
                ctrl->getModel()->setVideoEngineRequirements(inp, NULL);

            }
        }
        int i=0;
        bool foundSrc=false;
        while(i<nodes.size()){
            Node_ui* n=nodes[i];
            QPointF ep=mapToScene(event->pos());
            QPointF evpt=n->mapFromScene(ep);

            if(n->isNearby(evpt) && n->getNode()->getName()!=arrow_dragged->getDest()->getNode()->getName()){
                if(n->getNode()->isOutputNode() && arrow_dragged->getDest()->getNode()->isOutputNode()){
                    break;
                }
		
                if(n->getNode()->getFreeOutputNb()>0){
                    arrow_dragged->getDest()->getNode()->addParent(n->getNode());
                    arrow_dragged->getDest()->addParent(n);
                    n->getNode()->addChild(arrow_dragged->getDest()->getNode());
                    n->addChild(arrow_dragged->getDest());
                    n->getNode()->decrementFreeOutputNb();
                    arrow_dragged->setSource(n);
                    foundSrc=true;

                    break;
                }
            }
            i++;
        }
        if(!foundSrc){

            arrow_dragged->removeSource();
        }
        arrow_dragged->initLine();
        scene()->update();
        ctrl->getModel()->getVideoEngine()->clearRowCache();
        ctrl->getModel()->getVideoEngine()->clearPlayBackCache();
        if(foundSrc){
            /*if it is a viewer*/
            if(strcmp(arrow_dragged->getDest()->getNode()->class_name(),"Viewer")==0){
                std::vector<InputNode*> inputs;
                ctrl->getModel()->getGraphInput(inputs,dynamic_cast<Node*>(arrow_dragged->getSource()->getNode()));
                bool go=true;
                for(int i=0;i<inputs.size();i++){
                    InputNode* currentInput=inputs[i];
                    if(strcmp(currentInput->class_name(),"Reader")==0){
                        if(!(static_cast<Reader*>(currentInput)->hasFrames())){
                            go=false;
                        }
                    }
                }
                ctrl->getModel()->setVideoEngineRequirements(inputs,dynamic_cast<OutputNode*>(arrow_dragged->getDest()->getNode()));
                if(go){ // if there is an input upstream, we start the engine
                    ctrl->getModel()->startVideoEngine(1);
                }
                
            }
        }
        scene()->update();

    }
    else if(state==MOVING_AREA){
        //setCursor(QCursor(Qt::ArrowCursor));
        QGraphicsView::mouseReleaseEvent(event);

    }
    state=DEFAULT;
    setCursor(QCursor(Qt::ArrowCursor));
    viewport()->setCursor(QCursor(Qt::ArrowCursor));

}
void NodeGraph::mouseMoveEvent(QMouseEvent *event){
    QPointF newPos=mapToScene(event->pos());


    if(state==ARROW_DRAGGING){

        QPointF np=arrow_dragged->mapFromScene(newPos);
        arrow_dragged->updatePosition(np);
    }else if(state==MOVING_AREA){

        QGraphicsView::mouseMoveEvent(event);

    }else if(state==NODE_DRAGGING){
        QPointF op=node_dragged->mapFromScene(old_pos);
        QPointF np=node_dragged->mapFromScene(newPos);
        qreal diffx=np.x()-op.x();
        qreal diffy=np.y()-op.y();
        node_dragged->moveBy(diffx,diffy);
        foreach(Arrow* arrow,node_dragged->getInputsArrows()){
            arrow->initLine();
        }

        foreach(Node_ui* child,node_dragged->getChildren()){
            foreach(Arrow* arrow,child->getInputsArrows()){
                arrow->initLine();
            }
        }


    }
    old_pos=newPos;
    oldp=event->pos();

}
void NodeGraph::mouseDoubleClickEvent(QMouseEvent *event){
    int i=0;
    while(i<nodes.size()){
        Node_ui* n=nodes[i];

        QPointF evpt=n->mapFromScene(old_pos);
        if(n->contains(evpt) && strcmp(n->getNode()->class_name(),"Viewer")){
            if(!n->isThisPanelEnabled()){
                /*building settings panel*/
                n->setSettingsPanelEnabled(true);

                /*settings=new SettingsPanel(this);
                dockContainer->addWidget(settings);*/
                n->getSettingPanel()->setVisible(true);

                // needed for the layout to work correctly
                QWidget* pr=n->getDockContainer()->parentWidget();
                pr->setMinimumSize(n->getDockContainer()->sizeHint());

            }
            break;
        }
        i++;
    }

}

void NodeGraph::keyPressEvent(QKeyEvent *e){

    if(e->key() == Qt::Key_N  && nodeCreation_shortcut_enabled){


        if(smartNodeCreationEnabled){
            releaseKeyboard();
            SmartInputDialog* nodeCreation=new SmartInputDialog(ctrl,this);

            QPoint position=ctrl->getGui()->WorkShop->pos();
            position+=QPoint(ctrl->getGui()->width()/2,0);
            nodeCreation->move(position);
            setMouseTracking(false);

            nodeCreation->show();
            nodeCreation->raise();
            nodeCreation->activateWindow();


            smartNodeCreationEnabled=false;
        }


    }else if(e->key() == Qt::Key_R){
        try{
            
            ctrl->addNewNode(0,0,"Reader");
        }catch(...){
            std::cout << "(NodeGraph::keyPressEvent) Couldn't create reader. " << std::endl;
            
        }
        Node* reader = nodes[nodes.size()-1]->getNode();
        std::vector<Knob*> knobs = reader->getKnobs();
        foreach(Knob* k,knobs){
            if(k->getType() == FILE_KNOB){
                File_Knob* fk = static_cast<File_Knob*>(k);
                fk->open_file();
                break;
            }
        }
        
    }else if(e->key() == Qt::Key_Space){
        
        if(!_fullscreen){
            _fullscreen = true;
            ctrl->getGui()->viewer_tab->hide();
        }else{
            _fullscreen = false;
            ctrl->getGui()->viewer_tab->show();
        }
        
    }
    


}



