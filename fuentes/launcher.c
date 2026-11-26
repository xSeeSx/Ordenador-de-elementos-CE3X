#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define PATHCAP 32768
#define COMMANDCAP 65536
#define RESPONSECAP (8*1024*1024)
#define IDC_TITLE 111
#define IDC_VERSION 112
#define IDC_SOURCE 101
#define IDC_BROWSE 102
#define IDC_TREE 103
#define IDC_UP 104
#define IDC_DOWN 105
#define IDC_FIRST 106
#define IDC_LAST 107
#define IDC_UNDO 108
#define IDC_REDO 109
#define IDC_SAVE 110
#define IDC_STATUS 113
#define IDC_UPDATES 121
#define IDC_SLIDES 122
#define IDC_HINT 123
#define IDC_EXPAND 124
#define IDC_COLLAPSE 125
#define UI_BACKGROUND RGB(245,246,249)
#define UI_HEADER RGB(234,237,247)
#define UI_BORDER RGB(204,210,223)
#define UI_TEXT RGB(32,38,50)
#define UI_ACCENT RGB(58,69,154)
static HWND window,sourceEdit,tree,statusLabel,slideList;
static HFONT font,titleFont,strongFont,smallFont;
static HBRUSH backgroundBrush,panelBrush,headerBrush;
static int uiDpi=96,busy,operation,hasDocument,dirty,canUndo,canRedo,hasAttempt,lastResult;
static WCHAR baseDir[PATHCAP],sourcePath[PATHCAP],pendingPath[PATHCAP];
static HANDLE engineInput,engineOutput;
static PROCESS_INFORMATION updateProcess;
static HANDLE updateOutput;
static ULONGLONG updateStarted;
static DWORD updateSize;
static char updateReply[1024];
static int updateManual, updateReady, testMode;
static WCHAR updateVersion[32];
static PROCESS_INFORMATION engine;
static char response[RESPONSECAP];
static DWORD responseSize;
static ULONGLONG requestStarted;
typedef struct { char id[32]; HTREEITEM item; int expanded; } Node;
static Node *nodes; static int nodeCount;
static char dragId[32],lastSelected[32];
static int dragging;
static void layoutWindow(void);
static void refreshSlides(void);
static void updateMoveButtons(void);
static void syncSelection(void);
static int px(int v){return MulDiv(v,uiDpi,96);}
static int exists(const WCHAR *p){DWORD a=GetFileAttributesW(p);return a!=INVALID_FILE_ATTRIBUTES && !(a&FILE_ATTRIBUTE_DIRECTORY);}
static void report(const WCHAR *s){if(statusLabel)SetWindowTextW(statusLabel,s);}
static void setBusy(int value){
    busy=value;
    if(!window)return;
    EnableWindow(GetDlgItem(window,IDC_BROWSE),!value);
    EnableWindow(slideList,!value);
    for(int id=IDC_UP;id<=IDC_LAST;id++)EnableWindow(GetDlgItem(window,id),!value&&hasDocument);
    EnableWindow(GetDlgItem(window,IDC_UNDO),!value&&canUndo);
    EnableWindow(GetDlgItem(window,IDC_REDO),!value&&canRedo);
    EnableWindow(GetDlgItem(window,IDC_SAVE),!value&&hasDocument);
    updateMoveButtons();
}
static int addArg(WCHAR *command, size_t capacity, const WCHAR *value) {
    size_t length = wcslen(command);
    if (length + 4 >= capacity) return 0;
    command[length++] = L' '; command[length++] = L'"';
    while (*value) {
        size_t slashes = 0;
        while (*value == L'\\') { slashes++; value++; }
        size_t count = (*value == L'"' || !*value) ? 2 * slashes : slashes;
        if (*value == L'"') count++;
        if (length + count + 3 >= capacity) return 0;
        while (count--) command[length++] = L'\\';
        if (!*value) break;
        command[length++] = *value++;
    }
    command[length++] = L'"'; command[length] = 0; return 1;
}
static void closeUpdateCheck(void) {
    if (updateOutput) { CloseHandle(updateOutput); updateOutput = NULL; }
    if (updateProcess.hProcess) {
        DWORD code;
        if (GetExitCodeProcess(updateProcess.hProcess, &code) && code == STILL_ACTIVE)
            TerminateProcess(updateProcess.hProcess, 0);
        CloseHandle(updateProcess.hProcess); CloseHandle(updateProcess.hThread);
        ZeroMemory(&updateProcess, sizeof(updateProcess));
    }
}
static void openUpdateRelease(void) {
    WCHAR url[256];
    if (!updateVersion[0]) return;
    swprintf(url, ARRAYSIZE(url), L"https://github.com/xSeeSx/Ordenador-de-elementos-CE3X/releases/tag/v%ls", updateVersion);
    ShellExecuteW(window, L"open", url, NULL, NULL, SW_SHOWNORMAL);
}
static void applyUpdateReply(const char *reply, int manual) {
    HWND button = GetDlgItem(window, IDC_UPDATES);
    SetWindowTextW(button, L"Buscar actualizaciones"); EnableWindow(button, TRUE);
    unsigned major, minor, patch; int used = 0;
    if (sscanf(reply, "UPDATE\t%u.%u.%u%n", &major, &minor, &patch, &used) == 3
            && used > 0 && (reply[used] == 0 || reply[used] == '\r' || reply[used] == '\n')
            && major < 100000 && minor < 100000 && patch < 100000) {
        swprintf(updateVersion, ARRAYSIZE(updateVersion), L"%u.%u.%u", major, minor, patch);
        WCHAR label[96]; swprintf(label, ARRAYSIZE(label), L"Descargar v%ls", updateVersion);
        SetWindowTextW(button, label);
        SetWindowTextW(GetDlgItem(window, IDC_VERSION), L"v0.3.1  |  Nueva versión disponible");
        if (manual && MessageBoxW(window,
                L"Hay una versión nueva. ¿Quieres abrir su página de descarga?\r\n\r\nExtrae el ZIP nuevo en otra carpeta.",
                L"Actualización disponible", MB_YESNO | MB_ICONINFORMATION | MB_DEFBUTTON2) == IDYES)
            openUpdateRelease();
    } else if (manual) {
        MessageBoxW(window, strncmp(reply, "CURRENT\t", 8) == 0
            ? L"Ya tienes la última versión publicada."
            : L"No se pudo consultar GitHub. Puedes seguir usando el programa y volver a intentarlo más tarde.",
            L"Actualizaciones", MB_OK | MB_ICONINFORMATION);
    }
}
static void startUpdateCheck(int manual) {
    if (updateProcess.hProcess || updateReady) { if (manual) updateManual = 1; return; }
    if (updateVersion[0]) { openUpdateRelease(); return; }
    WCHAR python[PATHCAP], script[PATHCAP], command[32768] = L"";
    swprintf(python, PATHCAP, L"%ls\\runtime\\python.exe", baseDir);
    swprintf(script, PATHCAP, L"%ls\\app\\ordenador.py", baseDir);
    const WCHAR *arguments[] = {python, L"-I", L"-X", L"utf8", L"-B", script, L"check-updates"};
    for (int i = 0; i < 7; i++) if (!addArg(command, ARRAYSIZE(command), arguments[i])) return;
    SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
    HANDLE childOutput = NULL, childInput = NULL;
    if (!CreatePipe(&updateOutput, &childOutput, &security, 0) ||
        !SetHandleInformation(updateOutput, HANDLE_FLAG_INHERIT, 0)) goto fail;
    childInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, NULL);
    if (childInput == INVALID_HANDLE_VALUE) { childInput = NULL; goto fail; }
    STARTUPINFOW startup = {0}; startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
    startup.hStdInput = childInput; startup.hStdOutput = childOutput; startup.hStdError = childOutput;
    if (!CreateProcessW(python, command+1, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, baseDir, &startup, &updateProcess)) goto fail;
    CloseHandle(childOutput); CloseHandle(childInput);
    updateSize = 0; updateReply[0] = 0; updateReady = 0; updateManual = manual; updateStarted = GetTickCount64();
    SetWindowTextW(GetDlgItem(window, IDC_UPDATES), L"Buscando actualizaciones...");
    SetTimer(window, 2, 100, NULL); return;
fail:
    if (childOutput) CloseHandle(childOutput);
    if (childInput) CloseHandle(childInput);
    closeUpdateCheck(); applyUpdateReply("UNAVAILABLE\t", manual);
}
static void pollUpdateCheck(void) {
    if (updateProcess.hProcess) {
        DWORD available = 0, got = 0, code = STILL_ACTIVE;
        if (PeekNamedPipe(updateOutput, NULL, 0, NULL, &available, NULL) && available) {
            DWORD space = sizeof(updateReply)-updateSize-1;
            if (space && ReadFile(updateOutput, updateReply+updateSize, min(available, space), &got, NULL)) updateSize += got;
            updateReply[updateSize] = 0;
        }
        if (GetTickCount64()-updateStarted > 10000) {
            strcpy(updateReply, "UNAVAILABLE\t"); closeUpdateCheck(); updateReady = 1;
        } else if (GetExitCodeProcess(updateProcess.hProcess, &code) && code != STILL_ACTIVE) {
            // Una última lectura recoge la respuesta escrita justo antes de salir.
            if (PeekNamedPipe(updateOutput, NULL, 0, NULL, &available, NULL) && available) {
                DWORD space = sizeof(updateReply)-updateSize-1;
                if (space && ReadFile(updateOutput, updateReply+updateSize, min(available, space), &got, NULL)) updateSize += got;
                updateReply[updateSize] = 0;
            }
            closeUpdateCheck(); updateReady = 1;
        }
    }
    if (updateReady && (!updateManual || (!busy && IsWindowEnabled(window)))) {
        updateReady = 0; KillTimer(window, 2); applyUpdateReply(updateReply, updateManual);
    }
}
static void closeEngine(void) {
    if (engineInput) { CloseHandle(engineInput); engineInput = NULL; }
    if (engineOutput) { CloseHandle(engineOutput); engineOutput = NULL; }
    if (engine.hProcess) {
        WaitForSingleObject(engine.hProcess, 1000);
        CloseHandle(engine.hProcess); CloseHandle(engine.hThread);
        ZeroMemory(&engine, sizeof(engine));
    }
}
static int startEngine(void) {
    DWORD code;
    if (engine.hProcess && GetExitCodeProcess(engine.hProcess, &code) && code == STILL_ACTIVE) return 1;
    closeEngine(); hasAttempt = 0;
    WCHAR python[PATHCAP], script[PATHCAP], command[32768] = L"";
    swprintf(python, PATHCAP, L"%ls\\runtime\\python.exe", baseDir);
    swprintf(script, PATHCAP, L"%ls\\app\\ordenador.py", baseDir);
    if (!exists(python) || !exists(script)) {
        report(L"Falta el motor. Extrae todo el ZIP y conserva las carpetas app y runtime junto al ejecutable."); return 0;
    }
    const WCHAR *arguments[] = {python, L"-I", L"-X", L"utf8", L"-B", L"-u", script, L"serve"};
    for (int i = 0; i < 8; i++) if (!addArg(command, 32768, arguments[i])) return 0;
    SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
    HANDLE childInput = NULL, childOutput = NULL;
    if (!CreatePipe(&engineOutput, &childOutput, &security, 0) ||
        !CreatePipe(&childInput, &engineInput, &security, 0)) goto fail;
    if (!SetHandleInformation(engineOutput, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(engineInput, HANDLE_FLAG_INHERIT, 0)) goto fail;
    STARTUPINFOW startup = {0}; startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = childInput; startup.hStdOutput = childOutput; startup.hStdError = childOutput;
    if (!CreateProcessW(python, command + 1, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, baseDir, &startup, &engine)) goto fail;
    CloseHandle(childInput); CloseHandle(childOutput); responseSize = 0; return 1;
fail:
    if (childInput) CloseHandle(childInput);
    if (childOutput) CloseHandle(childOutput);
    closeEngine(); report(L"No se pudo iniciar Python. Comprueba que has extraido todo el ZIP."); return 0;
}
// Serialize paths and notes as JSON strings, including quotes and line breaks.
static int appendJson(WCHAR *target, const WCHAR *text) {
    size_t size = wcslen(target);
    if (size + 2 >= COMMANDCAP) return 0;
    target[size++] = L'"';
    for (; *text; text++) {
        if (size + 256 >= COMMANDCAP) return 0;
        if (*text == L'"' || *text == L'\\') { target[size++] = L'\\'; target[size++] = *text; }
        else if (*text < 32) {
            swprintf(target + size, COMMANDCAP - size, L"\\u%04x", (unsigned int)*text); size += 6;
        } else target[size++] = *text;
    }
    target[size++] = L'"'; target[size] = 0; return 1;
}
static int request(const WCHAR *json, int kind) {
    if (!startEngine()) return 0;
    int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, json, -1, NULL, 0, NULL, NULL);
    if (length <= 0) { report(L"Hay caracteres no validos en las rutas o las notas."); return 0; }
    char *bytes = HeapAlloc(GetProcessHeap(), 0, length + 1);
    if (!bytes) return 0;
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, json, -1, bytes, length, NULL, NULL);
    bytes[length - 1] = '\n';
    DWORD sent = 0;
    BOOL ok = WriteFile(engineInput, bytes, length, &sent, NULL);
    HeapFree(GetProcessHeap(), 0, bytes);
    if (!ok || sent != (DWORD)length) { closeEngine(); report(L"No se pudo comunicar con el motor."); return 0; }
    lastResult=0; responseSize = 0; operation = kind; requestStarted=GetTickCount64(); setBusy(1);
    if (kind == 1) report(L"Abriendo archivo...");
    else if (kind == 2) report(L"Comprobando y guardando la copia...");
    if (window) SetTimer(window, 1, 100, NULL);
    return 1;
}
static void fillBox(HDC dc, RECT rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color); FillRect(dc, &rect, brush); DeleteObject(brush);
}
static void frameBox(HDC dc, RECT rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color); FrameRect(dc, &rect, brush); DeleteObject(brush);
}
static HFONT makeFont(int size, int weight) {
    return CreateFontW(-px(size), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}
static void paintWindow(HDC dc){
    RECT r;GetClientRect(window,&r);fillBox(dc,r,UI_BACKGROUND);
    RECT h={px(20),px(20),r.right-px(20),px(90)};fillBox(dc,h,UI_HEADER);frameBox(dc,h,UI_BORDER);
    RECT p={px(20),px(108),r.right-px(20),px(164)};fillBox(dc,p,RGB(255,255,255));frameBox(dc,p,UI_BORDER);
}
static void drawButton(const DRAWITEMSTRUCT *item) {
    int primary = item->CtlID == IDC_SAVE;
    int disabled = (item->itemState & ODS_DISABLED) != 0;
    int pressed = (item->itemState & ODS_SELECTED) != 0;
    COLORREF background = primary ? UI_ACCENT : RGB(250, 251, 253);
    if (pressed) background = primary ? RGB(43, 53, 125) : RGB(227, 231, 240);
    if (disabled) background = primary ? RGB(173, 180, 210) : RGB(239, 241, 245);
    fillBox(item->hDC, item->rcItem, background);
    frameBox(item->hDC, item->rcItem, primary ? background : UI_BORDER);
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, primary ? RGB(255, 255, 255) : (disabled ? RGB(150, 156, 169) : UI_TEXT));
    HFONT old = SelectObject(item->hDC, primary ? strongFont : font);
    WCHAR text[128]; GetWindowTextW(item->hwndItem, text, 128);
    RECT label = item->rcItem;
    if (pressed) OffsetRect(&label, px(1), px(1));
    DrawTextW(item->hDC, text, -1, &label, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if ((item->itemState & ODS_FOCUS) && !disabled) {
        RECT focus = item->rcItem; InflateRect(&focus, -px(4), -px(4)); DrawFocusRect(item->hDC, &focus);
    }
    SelectObject(item->hDC, old);
}
static HWND control(const WCHAR *className, const WCHAR *text, DWORD style, int x, int y, int width, int height, int id) {
    if (wcscmp(className, L"STATIC") == 0) style |= SS_NOPREFIX;
    if (wcscmp(className, L"BUTTON") == 0) style = (style & ~BS_TYPEMASK) | BS_OWNERDRAW;
    if (wcscmp(className, L"EDIT") == 0 && id != IDC_STATUS) style |= WS_BORDER;
    HWND item = CreateWindowExW(0, className, text, WS_CHILD | WS_VISIBLE | style,
        px(x), px(y), px(width), px(height), window, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(item, WM_SETFONT, (WPARAM)font, TRUE); return item;
}
static int savePreview(HWND target, const WCHAR *path) {
    RECT rect; GetWindowRect(target, &rect);
    int width = rect.right - rect.left, height = rect.bottom - rect.top;
    HDC screen = GetDC(target), memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
    if (!memory || !bitmap) { if (memory) DeleteDC(memory); if (bitmap) DeleteObject(bitmap); ReleaseDC(target, screen); return 0; }
    HGDIOBJ old = SelectObject(memory, bitmap);
    BOOL painted = PrintWindow(target, memory, 0);
    SelectObject(memory, old);
    BITMAPINFO info = {0}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    DWORD size = (DWORD)width * (DWORD)height * 4;
    void *pixels = HeapAlloc(GetProcessHeap(), 0, size); int saved = 0;
    if (pixels && painted && GetDIBits(screen, bitmap, 0, height, pixels, &info, DIB_RGB_COLORS)) {
        FILE *file = _wfopen(path, L"wb");
        if (file) {
            BITMAPFILEHEADER header = {0}; header.bfType = 0x4d42;
            header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader); header.bfSize = header.bfOffBits + size;
            saved = fwrite(&header, sizeof(header), 1, file) == 1 &&
                fwrite(&info.bmiHeader, sizeof(info.bmiHeader), 1, file) == 1 && fwrite(pixels, size, 1, file) == 1;
            fclose(file);
        }
    }
    if (pixels) HeapFree(GetProcessHeap(), 0, pixels);
    DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(target, screen); return saved;
}

static WCHAR *unhex(const char *s){
    size_t n=strlen(s);if(n%2||n>RESPONSECAP)return NULL;
    char *bytes=malloc(n/2+1);if(!bytes)return NULL;
    for(size_t i=0;i<n;i+=2){unsigned v;if(sscanf(s+i,"%2x",&v)!=1){free(bytes);return NULL;}bytes[i/2]=(char)v;}
    bytes[n/2]=0;int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes,-1,NULL,0);
    WCHAR *result=count?malloc(count*sizeof(WCHAR)):NULL;
    if(result)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes,-1,result,count);free(bytes);return result;
}
static int movable(const char *id){return id && strchr("ewb",id[0]) && id[1]>='0' && id[1]<='9';}
static const char *itemId(HTREEITEM h){
    if(!h)return NULL;TVITEMW v={0};v.mask=TVIF_PARAM;v.hItem=h;
    if(!TreeView_GetItem(tree,&v)||v.lParam<0||v.lParam>=nodeCount)return NULL;return nodes[v.lParam].id;
}
static HTREEITEM findItem(const char *id){for(int i=0;i<nodeCount;i++)if(!strcmp(nodes[i].id,id))return nodes[i].item;return NULL;}
static int renderTree(char *rows,const char *selection){
    char selected[32]="";const char *old=itemId(TreeView_GetSelection(tree));if(old)strncpy(selected,old,31);
    if(selection && strcmp(selection,"-"))strncpy(selected,selection,31);
    Node *previous=nodes;int previousCount=nodeCount;
    for(int i=0;i<previousCount;i++)previous[i].expanded=(TreeView_GetItemState(tree,previous[i].item,TVIS_EXPANDED)&TVIS_EXPANDED)!=0;
    int count=1;for(char *s=rows;*s;s++)if(*s==';')count++;
    if(!strcmp(rows,"-"))count=0;
    Node *next=calloc(count?count:1,sizeof(Node));if(!next)return 0;
    SendMessageW(tree,WM_SETREDRAW,FALSE,0);TreeView_DeleteAllItems(tree);nodes=next;nodeCount=0;
    char *state=NULL,*row=strtok_s(rows,";",&state);
    while(row && strcmp(row,"-")){
        char *p=strchr(row,','),*q=p?strchr(p+1,','):NULL;if(!p||!q)goto fail;*p=0;*q=0;
        if(strlen(row)>31)goto fail;
        WCHAR *label=unhex(q+1);if(!label)goto fail;
        HTREEITEM parent=!strcmp(p+1,"-")?TVI_ROOT:findItem(p+1);
        if(!parent){free(label);goto fail;}
        strcpy(nodes[nodeCount].id,row);
        TVINSERTSTRUCTW ins={0};ins.hParent=parent;ins.hInsertAfter=TVI_LAST;ins.item.mask=TVIF_TEXT|TVIF_PARAM;
        ins.item.pszText=label;ins.item.lParam=nodeCount;nodes[nodeCount].item=TreeView_InsertItem(tree,&ins);free(label);
        int expanded=operation==1;
        if(operation!=1)for(int j=0;j<previousCount;j++)if(!strcmp(previous[j].id,row)){expanded=previous[j].expanded;break;}
        nodes[nodeCount].expanded=expanded;nodeCount++;row=strtok_s(NULL,";",&state);
    }
    for(int i=0;i<nodeCount;i++)if(nodes[i].expanded)TreeView_Expand(tree,nodes[i].item,TVE_EXPAND);
    HTREEITEM chosen=findItem(selected);if(chosen){TreeView_SelectItem(tree,chosen);TreeView_EnsureVisible(tree,chosen);}
    else if(nodeCount)TreeView_SelectItem(tree,nodes[0].item);
    free(previous);SendMessageW(tree,WM_SETREDRAW,TRUE,0);InvalidateRect(tree,NULL,TRUE);return 1;
fail:
    free(previous);SendMessageW(tree,WM_SETREDRAW,TRUE,0);InvalidateRect(tree,NULL,TRUE);return 0;
}
static void received(void){
    response[responseSize]=0;char *newline=strchr(response,'\n');if(newline)*newline=0;
    size_t n=strlen(response);if(n && response[n-1]=='\r')response[n-1]=0;
    char *state=NULL,*tag=strtok_s(response,"\t",&state);
    if(tag && !strcmp(tag,"ERROR")){
        char *h=strtok_s(NULL,"\t",&state);WCHAR *m=h?unhex(h):NULL;
        report(m?m:L"No se pudo completar la operación.");setBusy(0);
        if(!testMode)MessageBoxW(window,m?m:L"No se pudo completar la operación.",L"Ordenador CE3X",MB_OK|MB_ICONWARNING);
        free(m);return;
    }
    char *sel=strtok_s(NULL,"\t",&state),*flags=strtok_s(NULL,"\t",&state),*message=strtok_s(NULL,"\t",&state),*rows=strtok_s(NULL,"\t",&state);
    if(!tag||strcmp(tag,"OK")||!sel||!flags||!message||!rows){report(L"Respuesta del motor no válida.");setBusy(0);return;}
    sscanf(flags,"%d,%d,%d",&dirty,&canUndo,&canRedo);
    if(!renderTree(rows,sel)){report(L"No se pudo mostrar el árbol.");setBusy(0);return;}
    hasDocument=1;lastResult=1;refreshSlides();
    if(operation==1){wcscpy(sourcePath,pendingPath);SetWindowTextW(sourceEdit,sourcePath);}
    if(strcmp(message,"-")){WCHAR *m=unhex(message);report(m?m:L"Operación completada.");free(m);}
    else report(dirty?L"Cambios pendientes de guardar.":L"Sin cambios pendientes.");
    WCHAR title[256];swprintf(title,256,L"Ordenador de elementos CE3X%s",dirty?L" *":L"");SetWindowTextW(window,title);
    setBusy(0);
}
static void pollEngine(void){
    if(!busy||!engineOutput)return;
    DWORD available=0;
    if(!PeekNamedPipe(engineOutput,NULL,0,NULL,&available,NULL))goto broken;
    while(available){
        DWORD count=0,room=RESPONSECAP-1-responseSize;
        if(!room){report(L"El árbol supera el tamaño admitido.");closeEngine();setBusy(0);return;}
        if(!ReadFile(engineOutput,response+responseSize,available<room?available:room,&count,NULL)||!count)goto broken;
        responseSize+=count;response[responseSize]=0;
        if(strchr(response,'\n')){KillTimer(window,1);received();return;}
        if(!PeekNamedPipe(engineOutput,NULL,0,NULL,&available,NULL))goto broken;
    }
    return;
broken:
    report(L"El motor se ha detenido. Vuelve a abrir el archivo para continuar.");closeEngine();setBusy(0);
    hasDocument=0;setBusy(0);
}
static int discardChanges(void){return !dirty||MessageBoxW(window,L"Hay cambios sin guardar. ¿Quieres descartarlos?",L"Ordenador CE3X",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDYES;}
static int openPath(const WCHAR *path){
    WCHAR json[COMMANDCAP]=L"{\"command\":\"open\",\"path\":";
    if(!appendJson(json,path))return 0;wcscat(json,L"}");wcscpy(pendingPath,path);return request(json,1);
}
static void chooseSource(void){
    if(!discardChanges())return;
    WCHAR path[PATHCAP]=L"";OPENFILENAMEW ofn={0};ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=window;
    ofn.lpstrFilter=L"Archivos CE3X (*.cex)\0*.cex\0Todos los archivos\0*.*\0";ofn.lpstrFile=path;ofn.nMaxFile=PATHCAP;ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(GetOpenFileNameW(&ofn))openPath(path);
}
static int savePath(const WCHAR *path){
    WCHAR json[COMMANDCAP]=L"{\"command\":\"save\",\"overwrite\":true,\"path\":";
    if(!appendJson(json,path))return 0;wcscat(json,L"}");return request(json,2);
}
static void chooseSave(void){
    WCHAR path[PATHCAP];wcscpy(path,sourcePath);WCHAR *dot=wcsrchr(path,L'.'),*slash=wcsrchr(path,L'\\');if(dot&&(!slash||dot>slash))*dot=0;
    if(wcslen(path)+16>=PATHCAP){report(L"Ruta demasiado larga.");return;}wcscat(path,L"_ORDENADO.cex");
    OPENFILENAMEW ofn={0};ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=window;ofn.lpstrFilter=L"Archivos CE3X (*.cex)\0*.cex\0";
    ofn.lpstrFile=path;ofn.nMaxFile=PATHCAP;ofn.lpstrDefExt=L"cex";ofn.lpstrTitle=L"Guardar copia ordenada";ofn.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(GetSaveFileNameW(&ofn))savePath(path);
}
static int moveId(const char *id,const char *direction,const char *before){
    if(!movable(id)){report(L"Selecciona un cerramiento, un hueco o un puente térmico.");return 0;}
    WCHAR json[1024];
    if(before)swprintf(json,1024,L"{\"command\":\"move\",\"id\":\"%hs\",\"before\":\"%hs\"}",id,before);
    else swprintf(json,1024,L"{\"command\":\"move\",\"id\":\"%hs\",\"direction\":\"%hs\"}",id,direction);
    return request(json,3);
}
static void moveSelected(const char *direction){moveId(itemId(TreeView_GetSelection(tree)),direction,NULL);}
#include "sliding_list.h"
static void layoutWindow(void){
    if(!slideList)return;RECT r;GetClientRect(window,&r);int w=MulDiv(r.right,96,uiDpi),h=MulDiv(r.bottom,96,uiDpi);
    MoveWindow(GetDlgItem(window,IDC_TITLE),px(36),px(31),px(w-72),px(33),TRUE);
    MoveWindow(GetDlgItem(window,IDC_VERSION),px(37),px(66),px(w-300),px(18),TRUE);
    MoveWindow(GetDlgItem(window,IDC_UPDATES),px(w-250),px(53),px(214),px(28),TRUE);
    MoveWindow(sourceEdit,px(36),px(121),px(w-218),px(30),TRUE);MoveWindow(GetDlgItem(window,IDC_BROWSE),px(w-170),px(121),px(134),px(30),TRUE);
    MoveWindow(GetDlgItem(window,IDC_HINT),px(20),px(178),px(w-40),px(24),TRUE);
    MoveWindow(slideList,px(20),px(208),px(w-196),px(h-274),TRUE);
    int ids[]={IDC_UP,IDC_DOWN,IDC_FIRST,IDC_LAST,IDC_UNDO,IDC_REDO};
    for(int i=0;i<6;i++)MoveWindow(GetDlgItem(window,ids[i]),px(w-160),px(208+i*35+(i>=4?8:0)),px(140),px(29),TRUE);
    MoveWindow(GetDlgItem(window,IDC_EXPAND),px(w-160),px(434),px(140),px(27),TRUE);
    MoveWindow(GetDlgItem(window,IDC_COLLAPSE),px(w-160),px(465),px(140),px(27),TRUE);
    MoveWindow(GetDlgItem(window,IDC_SAVE),px(w-200),px(h-50),px(180),px(32),TRUE);
    MoveWindow(statusLabel,px(20),px(h-51),px(w-240),px(40),TRUE);InvalidateRect(window,NULL,TRUE);
}
static void initializeUI(void){
    font=makeFont(15,FW_NORMAL);titleFont=makeFont(25,FW_SEMIBOLD);strongFont=makeFont(15,FW_SEMIBOLD);smallFont=makeFont(13,FW_NORMAL);
    backgroundBrush=CreateSolidBrush(UI_BACKGROUND);panelBrush=CreateSolidBrush(RGB(255,255,255));headerBrush=CreateSolidBrush(UI_HEADER);
    HWND title=control(L"STATIC",L"Ordenador de elementos CE3X",SS_CENTERIMAGE,36,31,688,33,IDC_TITLE);SendMessageW(title,WM_SETFONT,(WPARAM)titleFont,TRUE);
    HWND version=control(L"STATIC",L"v0.3.1  |  Residencial 2.3 y 3.2",0,37,66,500,18,IDC_VERSION);SendMessageW(version,WM_SETFONT,(WPARAM)smallFont,TRUE);
    control(L"BUTTON",L"Buscar actualizaciones",WS_TABSTOP,570,53,214,28,IDC_UPDATES);
    sourceEdit=control(L"EDIT",L"",ES_READONLY|ES_AUTOHSCROLL|WS_TABSTOP,36,121,550,30,IDC_SOURCE);
    control(L"BUTTON",L"Abrir .cex...",WS_TABSTOP,598,121,126,30,IDC_BROWSE);
    tree=control(WC_TREEVIEWW,L"",WS_TABSTOP|WS_BORDER|TVS_HASLINES|TVS_LINESATROOT|TVS_HASBUTTONS|TVS_SHOWSELALWAYS,20,180,564,370,IDC_TREE);
    ShowWindow(tree,SW_HIDE);initializeSlides();
    HWND hint=control(L"STATIC",L"Arrastra las filas para ordenar  |  Ctrl + ↑ / ↓  |  Esc cancela",0,20,178,700,24,IDC_HINT);
    SendMessageW(hint,WM_SETFONT,(WPARAM)smallFont,TRUE);
    const WCHAR *labels[]={L"Subir",L"Bajar",L"Al inicio",L"Al final",L"Deshacer",L"Rehacer"};int ids[]={IDC_UP,IDC_DOWN,IDC_FIRST,IDC_LAST,IDC_UNDO,IDC_REDO};
    for(int i=0;i<6;i++)control(L"BUTTON",labels[i],WS_TABSTOP,600,180+i*42,140,32,ids[i]);
    control(L"BUTTON",L"Expandir todo",WS_TABSTOP,600,434,140,27,IDC_EXPAND);
    control(L"BUTTON",L"Contraer todo",WS_TABSTOP,600,465,140,27,IDC_COLLAPSE);
    control(L"BUTTON",L"Guardar copia...",WS_TABSTOP,560,550,180,32,IDC_SAVE);
    statusLabel=control(L"STATIC",L"",0,20,552,500,40,IDC_STATUS);SendMessageW(statusLabel,WM_SETFONT,(WPARAM)smallFont,TRUE);
    layoutWindow();setBusy(0);DragAcceptFiles(window,TRUE);
}
static LRESULT CALLBACK windowProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_CREATE:window=hwnd;initializeUI();return 0;
    case WM_SIZE:layoutWindow();return 0;
    case WM_GETMINMAXINFO:{MINMAXINFO *m=(MINMAXINFO*)lp;m->ptMinTrackSize.x=px(700);m->ptMinTrackSize.y=px(625);return 0;}
    case WM_COMMAND:
        if(LOWORD(wp)==IDC_UPDATES){startUpdateCheck(1);return 0;}
        if(busy)return 0;
        switch(LOWORD(wp)){
        case IDC_BROWSE:chooseSource();break;
        case IDC_EXPAND:expandAll(1);break;case IDC_COLLAPSE:expandAll(0);break;
        case IDC_SAVE:chooseSave();break;
        case IDC_UP:moveSelected("up");break;case IDC_DOWN:moveSelected("down");break;
        case IDC_FIRST:moveSelected("first");break;case IDC_LAST:moveSelected("last");break;
        case IDC_UNDO:request(L"{\"command\":\"undo\"}",3);break;case IDC_REDO:request(L"{\"command\":\"redo\"}",3);break;
        }return 0;
    case WM_NOTIFY:{NMHDR *n=(NMHDR*)lp;
        if(n->hwndFrom==tree&&n->code==TVN_SELCHANGEDW&&!busy)syncSelection();
        return 0;}
    case WM_SETTINGCHANGE:SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&motion,0);return 0;
    case WM_KEYDOWN:if(wp==VK_ESCAPE)cancelSlide(1);return 0;
    case WM_DROPFILES:{HDROP d=(HDROP)wp;WCHAR p[PATHCAP];if(!busy&&DragQueryFileW(d,0,p,PATHCAP)&&discardChanges())openPath(p);DragFinish(d);return 0;}
    case WM_TIMER:if(wp==3)tickSlides();else if(wp==2)pollUpdateCheck();else pollEngine();return 0;
    case WM_PAINT:{PAINTSTRUCT p;HDC dc=BeginPaint(hwnd,&p);paintWindow(dc);EndPaint(hwnd,&p);return 0;}
    case WM_PRINTCLIENT:paintWindow((HDC)wp);return 0;
    case WM_MEASUREITEM:if(wp==IDC_SLIDES){((MEASUREITEMSTRUCT*)lp)->itemHeight=px(31);return TRUE;}break;
    case WM_DRAWITEM:if(wp!=IDC_SLIDES)drawButton((const DRAWITEMSTRUCT*)lp);return TRUE;
    case WM_CTLCOLORSTATIC:{int id=GetDlgCtrlID((HWND)lp);HDC dc=(HDC)wp;int head=id==IDC_TITLE||id==IDC_VERSION;
        SetTextColor(dc,id==IDC_STATUS||id==IDC_VERSION?RGB(91,100,118):UI_TEXT);SetBkColor(dc,head?UI_HEADER:UI_BACKGROUND);return (LRESULT)(head?headerBrush:backgroundBrush);}
    case WM_CLOSE:if(busy){report(L"Espera a que termine la operación.");return 0;}if(discardChanges())DestroyWindow(hwnd);return 0;
    case WM_DESTROY:KillTimer(window,3);closeUpdateCheck();closeEngine();free(rows);free(nodes);DeleteObject(font);DeleteObject(titleFont);DeleteObject(strongFont);DeleteObject(smallFont);DeleteObject(backgroundBrush);DeleteObject(panelBrush);DeleteObject(headerBrush);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
static int waitRequest(void){
    ULONGLONG started=GetTickCount64();MSG msg;
    while(busy&&GetTickCount64()-started<30000){while(PeekMessageW(&msg,NULL,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}pollEngine();Sleep(10);}
    return !busy&&hasDocument&&lastResult;
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE previous,PWSTR cmd,int show){
    (void)previous;(void)cmd;SetProcessDPIAware();HDC dc=GetDC(NULL);uiDpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(NULL,dc);
    GetModuleFileNameW(NULL,baseDir,PATHCAP);WCHAR *slash=wcsrchr(baseDir,L'\\');if(slash)*slash=0;
    INITCOMMONCONTROLSEX common={sizeof(common),ICC_TREEVIEW_CLASSES};InitCommonControlsEx(&common);
    WNDCLASSW wc={0};wc.lpfnWndProc=windowProc;wc.hInstance=instance;wc.lpszClassName=L"CE3XOrderWindow";wc.hCursor=LoadCursor(NULL,IDC_ARROW);wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));
    if(!wc.hIcon)wc.hIcon=LoadIcon(NULL,IDI_APPLICATION);RegisterClassW(&wc);
    RECT r={0,0,px(820),px(650)};AdjustWindowRect(&r,WS_OVERLAPPEDWINDOW,FALSE);
    window=CreateWindowExW(0,wc.lpszClassName,L"Ordenador de elementos CE3X",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,r.right-r.left,r.bottom-r.top,NULL,NULL,instance,NULL);
    if(!window)return 1;
    HICON smallIcon=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED);
    if(smallIcon)SendMessageW(window,WM_SETICON,ICON_SMALL,(LPARAM)smallIcon);
    ShowWindow(window,show);UpdateWindow(window);
    int argc;WCHAR **argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argc>=5&&!wcscmp(argv[1],L"--self-test")){
        testMode=1;
        int ok=FindResourceW(instance,MAKEINTRESOURCEW(101),RT_GROUP_ICON)!=NULL&&smallIcon!=NULL&&openPath(argv[2])&&waitRequest();
        if(ok){selectRow(idIndex("e2"));SendMessageW(window,WM_COMMAND,IDC_FIRST,0);ok=waitRequest()&&dirty;}
        if(ok){SendMessageW(window,WM_COMMAND,IDC_UNDO,0);ok=waitRequest()&&!dirty;}
        if(ok){SendMessageW(window,WM_COMMAND,IDC_REDO,0);ok=waitRequest()&&dirty;}
        if(ok){
            int src=idIndex("w2"),dst=idIndex("w0");
            ok=src>=0&&dst>=0&&compatible(src,dst)&&!compatible(src,idIndex("w1"));
            if(ok){
                selectRow(src);startDrag(src,rowHeight/2);boundary=dst;retarget();
                ok=targetIndex(dst)==dst+1&&finishDrag()&&waitRequest();
            }
            if(ok){
                src=idIndex("e2");int other=idIndex("e0");startDrag(src,rowHeight/2);
                ok=span>1&&other>=0;boundary=blockEnd(other);retarget();
                ok=ok&&targetIndex(other)<other;cancelSlide(1);
                ok=ok&&dirty&&draggingRow<0;
            }
        }
        if(ok){
            int oldDirty=dirty,oldUndo=canUndo,oldRedo=canRedo; WCHAR label[96];
            applyUpdateReply("UNAVAILABLE\t",0);applyUpdateReply("CURRENT\t0.3.1",0);
            ok=!updateVersion[0];applyUpdateReply("UPDATE\t99.1.2\n",0);
            GetWindowTextW(GetDlgItem(window,IDC_UPDATES),label,ARRAYSIZE(label));
            ok=ok&&wcscmp(label,L"Descargar v99.1.2")==0&&dirty==oldDirty&&canUndo==oldUndo&&canRedo==oldRedo;
        }
        if(ok)ok=savePath(argv[3])&&waitRequest()&&!dirty&&exists(argv[3]);
        FILE *f=_wfopen(argv[4],L"wb");if(f){fputs(ok?"OK\n":"ERROR\n",f);if(!ok){WCHAR status[8192];char text[32768];GetWindowTextW(statusLabel,status,8192);WideCharToMultiByte(CP_UTF8,0,status,-1,text,32768,NULL,NULL);fputs(text,f);}fclose(f);}DestroyWindow(window);LocalFree(argv);return ok?0:1;
    }
    if(argc>=4&&!wcscmp(argv[1],L"--preview")){
        testMode=1;
        int ok=openPath(argv[2])&&waitRequest();
        if(ok){selectRow(idIndex("e2"));RedrawWindow(window,NULL,NULL,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);ok=savePreview(window,argv[3]);}
        DestroyWindow(window);LocalFree(argv);return ok?0:1;
    }
    if(argc>=2)openPath(argv[1]);LocalFree(argv);startUpdateCheck(0);MSG message;
    while(GetMessageW(&message,NULL,0,0)>0){
        if(message.message==WM_KEYDOWN&&message.wParam==VK_ESCAPE&&(pressed>=0||draggingRow>=0)){cancelSlide(1);continue;}
        if(message.message==WM_KEYDOWN && GetKeyState(VK_CONTROL)<0 && !busy){
            if(message.wParam=='Z'){if(canUndo)request(L"{\"command\":\"undo\"}",3);continue;}
            if(message.wParam=='Y'){if(canRedo)request(L"{\"command\":\"redo\"}",3);continue;}
            if(message.wParam=='S'){if(hasDocument)chooseSave();continue;}
            if(message.wParam=='O'){chooseSource();continue;}
        }
        if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
    }
    return (int)message.wParam;
}
