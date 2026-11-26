/* Native listbox supplies selection, scrollbars, keyboard and accessible names.
   A double-buffered renderer previews the order without modifying the CEX. */
typedef struct {
    HTREEITEM item,parent;
    char id[32];
    WCHAR label[1024];
    int depth;
    double y,from,to;
} SlideRow;
static SlideRow *rows;
static int count, rebuilding, hover=-1, rowHeight;
static WNDPROC nativeList;
static int pressed=-1, draggingRow=-1, span, boundary=-1, rejected;
static int mx,my,pressX,pressY,offset;
static BOOL motion=TRUE;
static ULONGLONG animationStart,lastScroll,flashUntil;
static char flashId[32];

static int selected(void){return (int)SendMessageW(slideList,LB_GETCURSEL,0,0);}
static int topIndex(void){int i=(int)SendMessageW(slideList,LB_GETTOPINDEX,0,0);return i<0?0:i;}
static int indexOf(HTREEITEM item){for(int i=0;i<count;i++)if(rows[i].item==item)return i;return -1;}
static int idIndex(const char *id){for(int i=0;i<count;i++)if(!strcmp(rows[i].id,id))return i;return -1;}
static int blockEnd(int i){int e=i+1;while(e<count&&rows[e].depth>rows[i].depth)e++;return e;}
static int compatible(int a,int b){
    return a>=0&&a<count&&b>=0&&b<count&&movable(rows[a].id)&&movable(rows[b].id)
        &&rows[a].parent==rows[b].parent&&rows[a].id[0]==rows[b].id[0];
}
static void updateMoveButtons(void){
    if(!slideList)return;
    int a=selected(),up=0,down=0;
    if(!busy&&hasDocument&&a>=0&&a<count&&movable(rows[a].id)){
        for(int i=0;i<count;i++)if(compatible(a,i)){if(i<a)up=1;if(i>a)down=1;}
    }
    EnableWindow(GetDlgItem(window,IDC_UP),up);EnableWindow(GetDlgItem(window,IDC_FIRST),up);
    EnableWindow(GetDlgItem(window,IDC_DOWN),down);EnableWindow(GetDlgItem(window,IDC_LAST),down);
    EnableWindow(GetDlgItem(window,IDC_EXPAND),!busy&&hasDocument);
    EnableWindow(GetDlgItem(window,IDC_COLLAPSE),!busy&&hasDocument);
}
static void selectRow(int i){
    if(i<0||i>=count)return;
    SendMessageW(slideList,LB_SETCURSEL,i,0);
    TreeView_SelectItem(tree,rows[i].item);
    updateMoveButtons();InvalidateRect(slideList,NULL,FALSE);
}
static void syncSelection(void){
    if(!slideList||rebuilding||busy)return;
    int i=indexOf(TreeView_GetSelection(tree));
    if(i>=0&&i!=selected())SendMessageW(slideList,LB_SETCURSEL,i,0);
    updateMoveButtons();InvalidateRect(slideList,NULL,FALSE);
}
static void collectRows(HTREEITEM first,int depth){
    for(HTREEITEM item=first;item;item=TreeView_GetNextSibling(tree,item)){
        if(count>=nodeCount)return;
        SlideRow *r=&rows[count];r->item=item;r->parent=TreeView_GetParent(tree,item);
        const char *id=itemId(item);if(!id)continue;
        strncpy(r->id,id,31);r->depth=depth;r->y=r->from=r->to=(double)count*rowHeight;
        TVITEMW v={0};v.mask=TVIF_TEXT;v.hItem=item;v.pszText=r->label;v.cchTextMax=ARRAYSIZE(r->label);
        TreeView_GetItem(tree,&v);
        WCHAR accessible[1200];swprintf(accessible,ARRAYSIZE(accessible),L"%ls%ls",
            movable(id)?(id[0]=='e'?L"Cerramiento: ":id[0]=='w'?L"Hueco: ":L"Puente térmico: "):L"",r->label);
        if(SendMessageW(slideList,LB_ADDSTRING,0,(LPARAM)accessible)<0)return;
        count++;
        if(TreeView_GetItemState(tree,item,TVIS_EXPANDED)&TVIS_EXPANDED)collectRows(TreeView_GetChild(tree,item),depth+1);
    }
}
static void refreshSlides(void){
    if(!slideList)return;
    char topId[32]="";int oldTop=topIndex();
    if(oldTop<count)strncpy(topId,rows[oldTop].id,31);
    SlideRow *next=calloc(nodeCount?nodeCount:1,sizeof(SlideRow));if(!next){report(L"No hay memoria para mostrar los elementos.");return;}
    rebuilding=1;SendMessageW(slideList,WM_SETREDRAW,FALSE,0);
    SendMessageW(slideList,LB_RESETCONTENT,0,0);free(rows);rows=next;count=0;hover=-1;
    HTREEITEM root=TreeView_GetRoot(tree);if(root)collectRows(TreeView_GetChild(tree,root),0);
    int i=indexOf(TreeView_GetSelection(tree));if(i<0&&count)i=0;
    if(i>=0){SendMessageW(slideList,LB_SETCURSEL,i,0);TreeView_SelectItem(tree,rows[i].item);}
    int t=idIndex(topId);RECT rc;GetClientRect(slideList,&rc);
    if(operation!=1&&t>=0&&i>=t&&i<t+max(1,rc.bottom/rowHeight))SendMessageW(slideList,LB_SETTOPINDEX,t,0);
    SendMessageW(slideList,WM_SETREDRAW,TRUE,0);rebuilding=0;updateMoveButtons();InvalidateRect(slideList,NULL,TRUE);
}
static void toggleRow(int i,int code){
    if(i<0||i>=count||!TreeView_GetChild(tree,rows[i].item))return;
    TreeView_SelectItem(tree,rows[i].item);TreeView_Expand(tree,rows[i].item,code);refreshSlides();
}
static void expandAll(int expand){
    HTREEITEM root=TreeView_GetRoot(tree);
    for(int i=0;i<nodeCount;i++)if(nodes[i].item!=root&&TreeView_GetChild(tree,nodes[i].item))
        TreeView_Expand(tree,nodes[i].item,expand?TVE_EXPAND:TVE_COLLAPSE);
    refreshSlides();
}
static int targetIndex(int i){
    if(draggingRow<0||boundary<0)return i;
    int end=draggingRow+span;
    if(boundary<draggingRow&&i>=boundary&&i<draggingRow)return i+span;
    if(boundary>end&&i>=end&&i<boundary)return i-span;
    return i;
}
static void retarget(void){
    animationStart=GetTickCount64();
    for(int i=0;i<count;i++){
        rows[i].from=rows[i].y;rows[i].to=(double)targetIndex(i)*rowHeight;
        if(!motion)rows[i].y=rows[i].to;
    }
    SetTimer(window,3,16,NULL);InvalidateRect(slideList,NULL,FALSE);
}
static void cancelSlide(int message){
    int active=draggingRow>=0;
    pressed=draggingRow=-1;span=0;boundary=-1;rejected=0;
    if(GetCapture()==slideList)ReleaseCapture();
    retarget();
    if(active&&message)report(dirty?L"Cambios pendientes de guardar.":L"Sin cambios pendientes.");
}
static void startDrag(int i,int y){
    if(i<0||i>=count||!movable(rows[i].id)||busy)return;
    draggingRow=i;span=blockEnd(i)-i;boundary=i;rejected=0;offset=y-(i-topIndex())*rowHeight;
    lastScroll=GetTickCount64();retarget();
    report(rows[i].id[0]=='e'?L"Moviendo el cerramiento y sus elementos asociados.":L"Arrastra dentro de su grupo. Esc cancela.");
}
static void chooseDrop(int x,int y){
    if(draggingRow<0)return;
    RECT rc;GetClientRect(slideList,&rc);int old=boundary,oldReject=rejected;
    boundary=-1;rejected=0;
    if(x>=0&&x<rc.right&&y>=0&&y<rc.bottom&&count){
        int pos=topIndex()*rowHeight+y;
        int a=min(count-1,pos/rowHeight);
        if(rows[draggingRow].id[0]=='e')while(a>=0&&rows[a].depth>rows[draggingRow].depth)a--;
        if(compatible(draggingRow,a)){
            int end=blockEnd(a);
            boundary=pos<(a+end)*rowHeight/2?a:end;
            if(boundary>=draggingRow&&boundary<=draggingRow+span)boundary=draggingRow;
        }else rejected=1;
    }
    if(old!=boundary||oldReject!=rejected){
        retarget();
        report(rejected?L"Destino incompatible. No se cambia el cerramiento asociado.":boundary<0?L"Fuera de la lista. Suelta para cancelar.":L"Suelta para insertar en el espacio indicado. Esc cancela.");
    }
    SetCursor(LoadCursor(NULL,boundary<0?IDC_NO:IDC_SIZEALL));InvalidateRect(slideList,NULL,FALSE);
}
static int finishDrag(void){
    if(draggingRow<0){cancelSlide(0);return 0;}
    int src=draggingRow,dest=boundary,end=src+span,invalid=rejected;
    char id[32],before[32]="";strcpy(id,rows[src].id);
    if(dest>=0&&dest!=src&&dest!=end)
        for(int i=dest;i<count;i++)if(i!=src&&compatible(src,i)){strcpy(before,rows[i].id);break;}
    cancelSlide(1);
    if(invalid){report(L"No se ha movido el elemento: destino incompatible.");return 0;}
    if(dest<0||dest==src||dest==end)return 0;
    strncpy(flashId,id,31);flashUntil=GetTickCount64()+650;
    return moveId(id,before[0]?NULL:"last",before[0]?before:NULL);
}
static void paintRow(HDC dc,int i,int y,int width,int floating){
    SlideRow *r=&rows[i];int x=px(8+r->depth*20),mid=y+rowHeight/2;
    int wall=movable(r->id)&&r->id[0]=='e',leaf=movable(r->id)&&!wall,sel=i==selected();
    RECT b={px(3),y+px(1),width-px(3),y+rowHeight-px(1)};
    COLORREF bg=floating||sel?RGB(227,233,252):i==hover&&draggingRow<0?RGB(239,242,250):wall?RGB(243,245,250):RGB(255,255,255);
    if(!strcmp(r->id,flashId)&&GetTickCount64()<flashUntil)bg=RGB(220,231,255);
    fillBox(dc,b,bg);
    if(wall||sel||floating){RECT bar={b.left,b.top,b.left+px(3),b.bottom};fillBox(dc,bar,sel||floating?UI_ACCENT:RGB(139,151,192));}
    if(sel&&GetFocus()==slideList&&!floating){RECT focus=b;InflateRect(&focus,-px(3),-px(2));frameBox(dc,focus,UI_ACCENT);}
    if(TreeView_GetChild(tree,r->item)){
        int exp=(TreeView_GetItemState(tree,r->item,TVIS_EXPANDED)&TVIS_EXPANDED)!=0;
        POINT a[3];
        if(exp){a[0]=(POINT){x+px(3),mid-px(2)};a[1]=(POINT){x+px(11),mid-px(2)};a[2]=(POINT){x+px(7),mid+px(3)};}
        else{a[0]=(POINT){x+px(4),mid-px(4)};a[1]=(POINT){x+px(10),mid};a[2]=(POINT){x+px(4),mid+px(4)};}
        HBRUSH brush=CreateSolidBrush(UI_TEXT);HGDIOBJ ob=SelectObject(dc,brush),op=SelectObject(dc,GetStockObject(NULL_PEN));Polygon(dc,a,3);SelectObject(dc,op);SelectObject(dc,ob);DeleteObject(brush);
    }
    if(movable(r->id))for(int j=0;j<3;j++)for(int k=0;k<2;k++){
        RECT dot={x+px(19+k*5),mid+px(-6+j*5),x+px(22+k*5),mid+px(-3+j*5)};fillBox(dc,dot,RGB(104,115,143));
    }
    int left=x+px(movable(r->id)?36:19);
    if(leaf){RECT dot={left,mid-px(3),left+px(6),mid+px(3)};fillBox(dc,dot,r->id[0]=='w'?RGB(74,117,157):RGB(151,109,63));left+=px(12);}
    HGDIOBJ old=SelectObject(dc,wall?strongFont:leaf?font:smallFont);
    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,wall||leaf?UI_TEXT:RGB(91,100,118));
    RECT text={left,y,width-px(10),y+rowHeight};
    DrawTextW(dc,r->label,-1,&text,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);SelectObject(dc,old);
}
static void paintSlides(HDC dc){
    RECT rc;GetClientRect(slideList,&rc);if(rc.right<=0||rc.bottom<=0)return;
    HDC mem=CreateCompatibleDC(dc);if(!mem)return;
    HBITMAP bmp=CreateCompatibleBitmap(dc,rc.right,rc.bottom);if(!bmp){DeleteDC(mem);return;}
    HGDIOBJ old=SelectObject(mem,bmp);fillBox(mem,rc,RGB(255,255,255));
    int scroll=topIndex()*rowHeight;
    if(!count){
        SetBkMode(mem,TRANSPARENT);SetTextColor(mem,RGB(91,100,118));SelectObject(mem,font);
        RECT blank=rc;InflateRect(&blank,-px(12),-px(12));DrawTextW(mem,L"Abre un certificado .cex para ordenar sus elementos.",-1,&blank,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    }
    if(draggingRow>=0&&boundary>=0){
        int slot=boundary>draggingRow+span?boundary-span:boundary;
        RECT gap={px(4),slot*rowHeight-scroll,rc.right-px(4),(slot+span)*rowHeight-scroll};
        fillBox(mem,gap,RGB(240,244,255));
    }
    for(int i=0;i<count;i++){
        if(draggingRow>=0&&i>=draggingRow&&i<draggingRow+span)continue;
        int y=(int)rows[i].y-scroll;
        if(y+rowHeight>0&&y<rc.bottom)paintRow(mem,i,y,rc.right,0);
    }
    if(draggingRow>=0){
        if(boundary>=0){
            int slot=boundary>draggingRow+span?boundary-span:boundary;
            RECT line={px(8+rows[draggingRow].depth*20),slot*rowHeight-scroll,rc.right-px(6),slot*rowHeight-scroll+px(3)};
            fillBox(mem,line,UI_ACCENT);
        }
        int y=my-offset,shown=min(span,4);
        RECT shadow={px(9),y+px(4),rc.right-px(2),y+(shown+(span>shown))*rowHeight+px(4)};
        fillBox(mem,shadow,RGB(204,211,228));
        for(int j=0;j<shown;j++)paintRow(mem,draggingRow+j,y+j*rowHeight,rc.right-px(7),1);
        if(span>shown){
            WCHAR more[80];swprintf(more,ARRAYSIZE(more),L"+ %d filas asociadas",span-shown);
            RECT note={px(4),y+shown*rowHeight,rc.right-px(10),y+(shown+1)*rowHeight};
            fillBox(mem,note,RGB(227,233,252));SetTextColor(mem,UI_TEXT);SetBkMode(mem,TRANSPARENT);SelectObject(mem,smallFont);
            DrawTextW(mem,more,-1,&note,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        }
    }
    BitBlt(dc,0,0,rc.right,rc.bottom,mem,0,0,SRCCOPY);SelectObject(mem,old);DeleteObject(bmp);DeleteDC(mem);
}
static void tickSlides(void){
    ULONGLONG now=GetTickCount64();
    if(draggingRow>=0&&now-lastScroll>=65){
        RECT rc;GetClientRect(slideList,&rc);int t=topIndex(),next=t;
        if(mx>=0&&mx<rc.right&&my>=0&&my<rc.bottom){
            if(my<px(26))next--;else if(my>rc.bottom-px(26))next++;
            if(next!=t){SendMessageW(slideList,LB_SETTOPINDEX,max(0,min(count-1,next)),0);chooseDrop(mx,my);}
        }
        lastScroll=now;
    }
    double t=motion?min(1.0,(double)(now-animationStart)/167.0):1.0;
    double eased=1.0-(1.0-t)*(1.0-t)*(1.0-t);
    for(int i=0;i<count;i++)rows[i].y=rows[i].from+(rows[i].to-rows[i].from)*eased;
    InvalidateRect(slideList,NULL,FALSE);
    if(t>=1.0&&draggingRow<0&&now>=flashUntil)KillTimer(window,3);
}
static LRESULT CALLBACK slideProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);paintSlides(dc);EndPaint(hwnd,&ps);return 0;}
    case WM_PRINTCLIENT:paintSlides((HDC)wp);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_GETDLGCODE:return DLGC_WANTARROWS|DLGC_WANTCHARS|(wp==VK_ESCAPE?DLGC_WANTMESSAGE:0);
    case WM_LBUTTONDOWN:{
        if(busy)return 0;
        int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp),i=topIndex()+y/rowHeight;
        if(y>=0&&i>=0&&i<count){
            SetFocus(hwnd);selectRow(i);
            if(x<px(8+rows[i].depth*20+17)&&TreeView_GetChild(tree,rows[i].item)){toggleRow(i,TVE_TOGGLE);return 0;}
            if(movable(rows[i].id)){pressed=i;pressX=mx=x;pressY=my=y;SetCapture(hwnd);}
        }return 0;}
    case WM_MOUSEMOVE:{
        mx=GET_X_LPARAM(lp);my=GET_Y_LPARAM(lp);
        if(pressed>=0&&draggingRow<0&&(abs(mx-pressX)>=max(px(4),GetSystemMetrics(SM_CXDRAG))||abs(my-pressY)>=max(px(4),GetSystemMetrics(SM_CYDRAG))))startDrag(pressed,pressY);
        if(draggingRow>=0){chooseDrop(mx,my);return 0;}
        int i=topIndex()+my/rowHeight;hover=my>=0&&i<count?i:-1;
        TRACKMOUSEEVENT tm={sizeof(tm),TME_LEAVE,hwnd,0};TrackMouseEvent(&tm);InvalidateRect(hwnd,NULL,FALSE);return 0;}
    case WM_MOUSELEAVE:hover=-1;InvalidateRect(hwnd,NULL,FALSE);return 0;
    case WM_LBUTTONUP:if(draggingRow>=0){chooseDrop(GET_X_LPARAM(lp),GET_Y_LPARAM(lp));finishDrag();}else if(pressed>=0)cancelSlide(0);return 0;
    case WM_CAPTURECHANGED:if(pressed>=0||draggingRow>=0)cancelSlide(1);return 0;
    case WM_CANCELMODE:if(pressed>=0||draggingRow>=0)cancelSlide(1);return 0;
    case WM_KEYDOWN:{
        if(wp==VK_ESCAPE&&(pressed>=0||draggingRow>=0)){cancelSlide(1);return 0;}
        if(busy)return 0;
        int i=selected();
        if(GetKeyState(VK_CONTROL)<0&&(wp==VK_UP||wp==VK_DOWN)){moveSelected(wp==VK_UP?"up":"down");return 0;}
        if(wp==VK_LEFT||wp==VK_RIGHT||wp==VK_RETURN||wp==VK_SPACE){
            if(i>=0&&i<count){
                if(wp==VK_LEFT&&!TreeView_GetChild(tree,rows[i].item))selectRow(indexOf(rows[i].parent));
                else toggleRow(i,wp==VK_LEFT?TVE_COLLAPSE:wp==VK_RIGHT?TVE_EXPAND:TVE_TOGGLE);
            }return 0;
        }break;}
    case WM_SETCURSOR:if(LOWORD(lp)==HTCLIENT){SetCursor(LoadCursor(NULL,draggingRow>=0?(boundary<0?IDC_NO:IDC_SIZEALL):hover>=0&&hover<count&&movable(rows[hover].id)?IDC_HAND:IDC_ARROW));return TRUE;}break;
    case WM_SETFOCUS:case WM_KILLFOCUS:InvalidateRect(hwnd,NULL,FALSE);break;
    }
    LRESULT result=CallWindowProcW(nativeList,hwnd,msg,wp,lp);
    if(!rebuilding&&(msg==WM_KEYDOWN||msg==WM_CHAR)){
        int i=selected();if(i>=0&&i<count)TreeView_SelectItem(tree,rows[i].item);updateMoveButtons();
    }
    if(msg==WM_KEYDOWN||msg==WM_CHAR||msg==WM_VSCROLL||msg==WM_MOUSEWHEEL||msg==LB_SETCURSEL||msg==LB_SETTOPINDEX||msg==WM_SIZE)InvalidateRect(hwnd,NULL,FALSE);
    return result;
}
static void initializeSlides(void){
    rowHeight=px(31);
    slideList=control(L"LISTBOX",L"",WS_TABSTOP|WS_BORDER|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS,20,208,564,342,IDC_SLIDES);
    SendMessageW(slideList,LB_SETITEMHEIGHT,0,rowHeight);
    nativeList=(WNDPROC)SetWindowLongPtrW(slideList,GWLP_WNDPROC,(LONG_PTR)slideProc);
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&motion,0);
}
