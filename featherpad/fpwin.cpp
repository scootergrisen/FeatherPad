/*
 * Copyright (C) Pedram Pourang (aka Tsu Jan) 2014 <tsujan2000@gmail.com>
 *
 * FeatherPad is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FeatherPad is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "singleton.h"
#include "ui_fp.h"
#include "encoding.h"
#include "filedialog.h"
#include "messagebox.h"
#include "pref.h"
#include "session.h"
#include "loading.h"
#include "warningbar.h"

#include <QFontDialog>
#include <QPrintDialog>
#include <QToolTip>
#include <QDesktopWidget>
#include <fstream> // std::ofstream
#include <QPrinter>

#include "x11.h"

namespace FeatherPad {

void BusyMaker::waiting() {
    QTimer::singleShot (timeout, this, SLOT (makeBusy()));
}

void BusyMaker::makeBusy() {
    if (QGuiApplication::overrideCursor() == nullptr)
        QGuiApplication::setOverrideCursor (Qt::WaitCursor);
    emit finished();
}


FPwin::FPwin (QWidget *parent):QMainWindow (parent), dummyWidget (nullptr), ui (new Ui::FPwin)
{
    ui->setupUi (this);

    loadingProcesses_ = 0;
    rightClicked_ = -1;
    busyThread_ = nullptr;

    /* JumpTo bar*/
    ui->spinBox->hide();
    ui->label->hide();
    ui->checkBox->hide();

    /* status bar */
    QLabel *statusLabel = new QLabel();
    statusLabel->setIndent(2);
    statusLabel->setTextInteractionFlags (Qt::TextSelectableByMouse);
    QToolButton *wordButton = new QToolButton();
    wordButton->setAutoRaise (true);
    wordButton->setToolButtonStyle (Qt::ToolButtonIconOnly);
    wordButton->setIconSize (QSize (16, 16));
    wordButton->setMaximumHeight (16);
    wordButton->setIcon (QIcon (":icons/view-refresh.svg"));
    //wordButton->setText (tr ("Refresh"));
    wordButton->setToolTip (tr ("Calculate number of words\n(For huge texts, this may be CPU-intensive.)"));
    connect (wordButton, &QAbstractButton::clicked, this, &FPwin::wordButtonStatus);
    ui->statusBar->addWidget (statusLabel);
    ui->statusBar->addWidget (wordButton);

    /* text unlocking */
    ui->actionEdit->setVisible (false);

    ui->actionRun->setVisible (false);

    /* replace dock */
    ui->dockReplace->setTabOrder (ui->lineEditFind, ui->lineEditReplace);
    ui->dockReplace->setTabOrder (ui->lineEditReplace, ui->toolButtonNext);
    /* tooltips are set here for easier translation */
    ui->toolButtonNext->setToolTip (tr ("Next") + " (" + tr ("F7") + ")");
    ui->toolButtonPrv->setToolTip (tr ("Previous") + " (" + tr ("F8") + ")");
    ui->toolButtonAll->setToolTip (tr ("Replace all") + " (" + tr ("F9") + ")");
    ui->dockReplace->setVisible (false);

    applyConfig();

    QWidget* spacer = new QWidget();
    spacer->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Preferred);
    ui->mainToolBar->insertWidget (ui->actionMenu, spacer);
    QMenu *menu = new QMenu (ui->mainToolBar);
    menu->addMenu (ui->menuFile);
    menu->addMenu (ui->menuEdit);
    menu->addMenu (ui->menuOptions);
    menu->addMenu (ui->menuSearch);
    menu->addMenu (ui->menuHelp);
    ui->actionMenu->setMenu(menu);
    QList<QToolButton*> tbList = ui->mainToolBar->findChildren<QToolButton*>();
    if (!tbList.isEmpty())
        tbList.at (tbList.count() - 1)->setPopupMode (QToolButton::InstantPopup);

    newTab();

    aGroup_ = new QActionGroup (this);
    ui->actionUTF_8->setActionGroup (aGroup_);
    ui->actionUTF_16->setActionGroup (aGroup_);
    ui->actionWindows_Arabic->setActionGroup (aGroup_);
    ui->actionISO_8859_1->setActionGroup (aGroup_);
    ui->actionISO_8859_15->setActionGroup (aGroup_);
    ui->actionWindows_1252->setActionGroup (aGroup_);
    ui->actionCryllic_CP1251->setActionGroup (aGroup_);
    ui->actionCryllic_KOI8_U->setActionGroup (aGroup_);
    ui->actionCryllic_ISO_8859_5->setActionGroup (aGroup_);
    ui->actionChineese_BIG5->setActionGroup (aGroup_);
    ui->actionChinese_GB18030->setActionGroup (aGroup_);
    ui->actionJapanese_ISO_2022_JP->setActionGroup (aGroup_);
    ui->actionJapanese_ISO_2022_JP_2->setActionGroup (aGroup_);
    ui->actionJapanese_ISO_2022_KR->setActionGroup (aGroup_);
    ui->actionJapanese_CP932->setActionGroup (aGroup_);
    ui->actionJapanese_EUC_JP->setActionGroup (aGroup_);
    ui->actionKorean_CP949->setActionGroup (aGroup_);
    ui->actionKorean_CP1361->setActionGroup (aGroup_);
    ui->actionKorean_EUC_KR->setActionGroup (aGroup_);
    ui->actionOther->setActionGroup (aGroup_);

    ui->actionUTF_8->setChecked (true);
    ui->actionOther->setDisabled (true);

    connect (ui->actionNew, &QAction::triggered, this, &FPwin::newTab);
    //connect (ui->tabWidget->tabBar(), &TabBar::newTab, this, &FPwin::newTab);
    connect (ui->actionDetachTab, &QAction::triggered, this, &FPwin::detachTab);
    connect (ui->actionRightTab, &QAction::triggered, this, &FPwin::nextTab);
    connect (ui->actionLeftTab, &QAction::triggered, this, &FPwin::previousTab);
    connect (ui->actionLastTab, &QAction::triggered, this, &FPwin::lastTab);
    connect (ui->actionFirstTab, &QAction::triggered, this, &FPwin::firstTab);
    connect (ui->actionClose, &QAction::triggered, this, &FPwin::closeTab);
    connect (ui->tabWidget, &QTabWidget::tabCloseRequested, this, &FPwin::closeTabAtIndex);
    connect (ui->actionOpen, &QAction::triggered, this, &FPwin::fileOpen);
    connect (ui->actionReload, &QAction::triggered, this, &FPwin::reload);
    connect (aGroup_, &QActionGroup::triggered, this, &FPwin::enforceEncoding);
    connect (ui->actionSave, &QAction::triggered, this, &FPwin::fileSave);
    connect (ui->actionSaveAs, &QAction::triggered, this, &FPwin::fileSave);
    connect (ui->actionSaveCodec, &QAction::triggered, this, &FPwin::fileSave);

    connect (ui->actionCut, &QAction::triggered, this, &FPwin::cutText);
    connect (ui->actionCopy, &QAction::triggered, this, &FPwin::copyText);
    connect (ui->actionPaste, &QAction::triggered, this, &FPwin::pasteText);
    connect (ui->actionDelete, &QAction::triggered, this, &FPwin::deleteText);
    connect (ui->actionSelectAll, &QAction::triggered, this, &FPwin::selectAllText);

    connect (ui->actionEdit, &QAction::triggered, this, &FPwin::makeEditable);

    connect (ui->actionSession, &QAction::triggered, this, &FPwin::manageSessions);

    connect (ui->actionRun, &QAction::triggered, this, &FPwin::executeProcess);

    connect (ui->actionUndo, &QAction::triggered, this, &FPwin::undoing);
    connect (ui->actionRedo, &QAction::triggered, this, &FPwin::redoing);

    connect (ui->tabWidget, &TabWidget::currentTabChanged, this, &FPwin::tabSwitch);
    connect (ui->tabWidget->tabBar(), &TabBar::tabDetached, this, &FPwin::detachTab);
    ui->tabWidget->tabBar()->setContextMenuPolicy (Qt::CustomContextMenu);
    connect (ui->tabWidget->tabBar(), &QWidget::customContextMenuRequested, this, &FPwin::tabContextMenu);
    connect (ui->actionCopyName, &QAction::triggered, this, &FPwin::copyTabFileName);
    connect (ui->actionCopyPath, &QAction::triggered, this, &FPwin::copyTabFilePath);
    connect (ui->actionCloseAll, &QAction::triggered, this, &FPwin::closeAllTabs);
    connect (ui->actionCloseRight, &QAction::triggered, this, &FPwin::closeNextTabs);
    connect (ui->actionCloseLeft, &QAction::triggered, this, &FPwin::closePreviousTabs);
    connect (ui->actionCloseOther, &QAction::triggered, this, &FPwin::closeOtherTabs);

    connect (ui->actionFont, &QAction::triggered, this, &FPwin::fontDialog);

    connect (ui->actionFind, &QAction::triggered, this, &FPwin::showHideSearch);
    connect (ui->actionJump, &QAction::triggered, this, &FPwin::jumpTo);
    connect (ui->spinBox, &QAbstractSpinBox::editingFinished, this, &FPwin::goTo);

    connect (ui->actionLineNumbers, &QAction::toggled, this, &FPwin::showLN);
    connect (ui->actionWrap, &QAction::triggered, this, &FPwin::toggleWrapping);
    connect (ui->actionSyntax, &QAction::triggered, this, &FPwin::toggleSyntaxHighlighting);
    connect (ui->actionIndent, &QAction::triggered, this, &FPwin::toggleIndent);

    connect (ui->actionPreferences, &QAction::triggered, this, &FPwin::prefDialog);

    connect (ui->actionReplace, &QAction::triggered, this, &FPwin::replaceDock);
    connect (ui->toolButtonNext, &QAbstractButton::clicked, this, &FPwin::replace);
    connect (ui->toolButtonPrv, &QAbstractButton::clicked, this, &FPwin::replace);
    connect (ui->toolButtonAll, &QAbstractButton::clicked, this, &FPwin::replaceAll);
    connect (ui->dockReplace, &QDockWidget::visibilityChanged, this, &FPwin::closeReplaceDock);
    connect (ui->dockReplace, &QDockWidget::topLevelChanged, this, &FPwin::resizeDock);

    connect (ui->actionDoc, &QAction::triggered, this, &FPwin::docProp);
    connect (ui->actionPrint, &QAction::triggered, this, &FPwin::filePrint);

    connect (ui->actionAbout, &QAction::triggered, this, &FPwin::aboutDialog);
    connect (ui->actionHelp, &QAction::triggered, this, &FPwin::helpDoc);

    /***************************************************************************
     *****     KDE (KAcceleratorManager) has a nasty "feature" that        *****
     *****   "smartly" gives mnemonics to tab and tool button texts so     *****
     *****   that, sometimes, the same mnemonics are disabled in the GUI   *****
     *****     and, as a result, their corresponding action shortcuts      *****
     *****     become disabled too. As a workaround, we don't set text     *****
     *****     for tool buttons on the search bar and replacement dock.    *****
     ***** The toolbar buttons and menu items aren't affected by this bug. *****
     ***************************************************************************/
    ui->toolButtonNext->setShortcut (QKeySequence (tr ("F7")));
    ui->toolButtonPrv->setShortcut (QKeySequence (tr ("F8")));
    ui->toolButtonAll->setShortcut (QKeySequence (tr ("F9")));

    QShortcut *zoomin = new QShortcut (QKeySequence (tr ("Ctrl+=", "Zoom in")), this);
    QShortcut *zoominPlus = new QShortcut (QKeySequence (tr ("Ctrl++", "Zoom in")), this);
    QShortcut *zoomout = new QShortcut (QKeySequence (tr ("Ctrl+-", "Zoom out")), this);
    QShortcut *zoomzero = new QShortcut (QKeySequence (tr ("Ctrl+0", "Zoom default")), this);
    connect (zoomin, &QShortcut::activated, this, &FPwin::zoomIn);
    connect (zoominPlus, &QShortcut::activated, this, &FPwin::zoomIn);
    connect (zoomout, &QShortcut::activated, this, &FPwin::zoomOut);
    connect (zoomzero, &QShortcut::activated, this, &FPwin::zoomZero);

    QShortcut *fullscreen = new QShortcut (QKeySequence (tr ("F11", "Fullscreen")), this);
    QShortcut *defaultsize = new QShortcut (QKeySequence (tr ("Ctrl+Shift+W", "Default size")), this);
    connect (fullscreen, &QShortcut::activated, this, &FPwin::fullScreening);
    connect (defaultsize, &QShortcut::activated, this, &FPwin::defaultSize);

    /* this is a workaround for the RTL bug in QPlainTextEdit */
    QShortcut *align = new QShortcut (QKeySequence (tr ("Ctrl+Shift+A", "Alignment")), this);
    connect (align, &QShortcut::activated, this, &FPwin::align);

    /* exiting a process */
    QShortcut *kill = new QShortcut (QKeySequence (tr ("Ctrl+Alt+E", "Kill process")), this);
    connect (kill, &QShortcut::activated, this, &FPwin::exitProcess);

    dummyWidget = new QWidget();
    setAcceptDrops (true);
    setAttribute (Qt::WA_AlwaysShowToolTips);
    setAttribute (Qt::WA_DeleteOnClose, false); // we delete windows in singleton
}
/*************************/
FPwin::~FPwin()
{
    delete dummyWidget; dummyWidget = nullptr;
    delete aGroup_; aGroup_ = nullptr;
    delete ui; ui = nullptr;
}
/*************************/
void FPwin::closeEvent (QCloseEvent *event)
{
    bool keep = closeTabs (-1, -1);
    if (keep)
        event->ignore();
    else
    {
        FPsingleton *singleton = static_cast<FPsingleton*>(qApp);
        Config& config = singleton->getConfig();
        if (config.getRemSize() && windowState() == Qt::WindowNoState)
            config.setWinSize (size());
        singleton->removeWin (this);
        event->accept();
    }
}
/*************************/
void FPwin::applyConfig()
{
    Config& config = static_cast<FPsingleton*>(qApp)->getConfig();

    if (config.getRemSize())
    {
        resize (config.getWinSize());
        if (config.getIsMaxed())
            setWindowState (Qt::WindowMaximized);
        if (config.getIsFull() && config.getIsMaxed())
            setWindowState (windowState() ^ Qt::WindowFullScreen);
        else if (config.getIsFull())
            setWindowState (Qt::WindowFullScreen);
    }
    else
    {
        QSize startSize = config.getStartSize();
        QSize ag = QApplication::desktop()->availableGeometry().size();
        if (startSize.width() > ag.width() || startSize.height() > ag.height())
        {
            startSize = startSize.boundedTo (ag);
            config.setStartSize (startSize);
        }
        else if (startSize.isEmpty())
        {
            startSize = QSize (700, 500);
            config.setStartSize (startSize);
        }
        resize (startSize);
    }

    ui->mainToolBar->setVisible (!config.getNoToolbar());
    ui->menuBar->setVisible (!config.getNoMenubar());
    ui->actionMenu->setVisible (config.getNoMenubar());

    ui->actionDoc->setVisible (!config.getShowStatusbar());

    ui->actionWrap->setChecked (config.getWrapByDefault());

    ui->actionIndent->setChecked (config.getIndentByDefault());

    ui->actionLineNumbers->setChecked (config.getLineByDefault());
    ui->actionLineNumbers->setDisabled (config.getLineByDefault());

    ui->actionSyntax->setChecked (config.getSyntaxByDefault());

    if (!config.getShowStatusbar())
        ui->statusBar->hide();

    if (config.getTabPosition() != 0)
        ui->tabWidget->setTabPosition ((QTabWidget::TabPosition) config.getTabPosition());

    ui->tabWidget->tabBar()->hideSingle (config.getHideSingleTab());

    if (config.getRecentOpened())
        ui->menuOpenRecently->setTitle (tr ("&Recently Opened"));
    int recentNumber = config.getCurRecentFilesNumber();
    QAction* recentAction = nullptr;
    for (int i = 0; i < recentNumber; ++i)
    {
        recentAction = new QAction (this);
        recentAction->setVisible (false);
        connect (recentAction, &QAction::triggered, this, &FPwin::newTabFromRecent);
        ui->menuOpenRecently->addAction (recentAction);
    }
    ui->menuOpenRecently->addAction (ui->actionClearRecent);
    connect (ui->menuOpenRecently, &QMenu::aboutToShow, this, &FPwin::updateRecenMenu);
    connect (ui->actionClearRecent, &QAction::triggered, this, &FPwin::clearRecentMenu);

    if (config.getIconless())
    {
        iconMode_ = NONE;
        ui->toolButtonNext->setText (tr ("Next"));
        ui->toolButtonPrv->setText (tr ("Previous"));
        ui->toolButtonAll->setText (tr ("All"));
    }
    else
    {
        bool rtl (QApplication::layoutDirection() == Qt::RightToLeft);
        if (config.getSysIcon())
        {
            iconMode_ = SYSTEM;

            ui->actionNew->setIcon (QIcon::fromTheme ("document-new"));
            ui->actionOpen->setIcon (QIcon::fromTheme ("document-open"));
            ui->menuOpenRecently->setIcon (QIcon::fromTheme ("document-open-recent"));
            ui->actionClearRecent->setIcon (QIcon::fromTheme ("edit-clear"));
            ui->actionSave->setIcon (QIcon::fromTheme ("document-save"));
            ui->actionSaveAs->setIcon (QIcon::fromTheme ("document-save-as"));
            ui->actionSaveCodec->setIcon (QIcon::fromTheme ("document-save-as"));
            ui->actionPrint->setIcon (QIcon::fromTheme ("document-print"));
            ui->actionDoc->setIcon (QIcon::fromTheme ("document-properties"));
            ui->actionUndo->setIcon (QIcon::fromTheme ("edit-undo"));
            ui->actionRedo->setIcon (QIcon::fromTheme ("edit-redo"));
            ui->actionCut->setIcon (QIcon::fromTheme ("edit-cut"));
            ui->actionCopy->setIcon (QIcon::fromTheme ("edit-copy"));
            ui->actionPaste->setIcon (QIcon::fromTheme ("edit-paste"));
            ui->actionDelete->setIcon (QIcon::fromTheme ("edit-delete"));
            ui->actionSelectAll->setIcon (QIcon::fromTheme ("edit-select-all"));
            ui->actionReload->setIcon (QIcon::fromTheme ("view-refresh"));
            ui->actionFind->setIcon (QIcon::fromTheme ("edit-find"));
            ui->actionReplace->setIcon (QIcon::fromTheme ("edit-find-replace"));
            ui->actionClose->setIcon (QIcon::fromTheme ("window-close"));
            ui->actionQuit->setIcon (QIcon::fromTheme ("application-exit"));
            ui->actionFont->setIcon (QIcon::fromTheme ("preferences-desktop-font"));
            ui->actionPreferences->setIcon (QIcon::fromTheme ("preferences-system"));
            ui->actionHelp->setIcon (QIcon::fromTheme ("help-contents"));
            ui->actionAbout->setIcon (QIcon::fromTheme ("help-about"));
            ui->actionJump->setIcon (QIcon::fromTheme ("go-jump"));
            ui->actionEdit->setIcon (QIcon::fromTheme ("document-edit"));
            ui->actionRun->setIcon (QIcon::fromTheme ("system-run"));
            ui->actionCopyName->setIcon (QIcon::fromTheme ("edit-copy"));
            ui->actionCopyPath->setIcon (QIcon::fromTheme ("edit-copy"));

            /* these icons may not exist in some themes... */
            QIcon icn = QIcon::fromTheme ("tab-close-other");
            if (icn.isNull())
                icn = QIcon (":icons/tab-close-other.svg");
            ui->actionCloseOther->setIcon (icn);
            icn = QIcon::fromTheme ("application-menu");
            if (icn.isNull())
                icn = QIcon (":icons/application-menu.svg");
            ui->actionMenu->setIcon (icn);
            /* ... and the following buttons don't have text, so we don't risk */
            icn = QIcon::fromTheme ("go-down");
            if (icn.isNull())
                icn = QIcon (":icons/go-down.svg");
             ui->toolButtonNext->setIcon (icn);
            icn = QIcon::fromTheme ("go-up");
            if (icn.isNull())
                icn = QIcon (":icons/go-up.svg");
            ui->toolButtonPrv->setIcon (icn);
            icn = QIcon::fromTheme ("arrow-down-double");
            if (icn.isNull())
                icn = QIcon (":icons/arrow-down-double.svg");
            ui->toolButtonAll->setIcon (icn);
            if (QToolButton *wordButton = ui->statusBar->findChild<QToolButton *>())
            {
                icn = QIcon::fromTheme ("view-refresh");
                if (!icn.isNull())
                    wordButton->setIcon (icn);
            }

            if (rtl)
            {
                ui->actionCloseRight->setIcon (QIcon::fromTheme ("go-previous"));
                ui->actionCloseLeft->setIcon (QIcon::fromTheme ("go-next"));
                ui->actionRightTab->setIcon (QIcon::fromTheme ("go-previous"));
                ui->actionLeftTab->setIcon (QIcon::fromTheme ("go-next"));

                /* shortcuts should be reversed for rtl */
                ui->actionRightTab->setShortcut (QKeySequence (tr ("Alt+Left")));
                ui->actionLeftTab->setShortcut (QKeySequence (tr ("Alt+Right")));
            }
            else
            {
                ui->actionCloseRight->setIcon (QIcon::fromTheme ("go-next"));
                ui->actionCloseLeft->setIcon (QIcon::fromTheme ("go-previous"));
                ui->actionRightTab->setIcon (QIcon::fromTheme ("go-next"));
                ui->actionLeftTab->setIcon (QIcon::fromTheme ("go-previous"));
            }

            icn = QIcon::fromTheme ("featherpad");
            if (icn.isNull())
                icn = QIcon (":icons/featherpad.svg");
            setWindowIcon (icn);
        }
        else // own icons
        {
            iconMode_ = OWN;

            ui->actionNew->setIcon (QIcon (":icons/document-new.svg"));
            ui->actionOpen->setIcon (QIcon (":icons/document-open.svg"));
            ui->menuOpenRecently->setIcon (QIcon (":icons/document-open-recent.svg"));
            ui->actionClearRecent->setIcon (QIcon (":icons/edit-clear.svg"));
            ui->actionSave->setIcon (QIcon (":icons/document-save.svg"));
            ui->actionSaveAs->setIcon (QIcon (":icons/document-save-as.svg"));
            ui->actionSaveCodec->setIcon (QIcon (":icons/document-save-as.svg"));
            ui->actionPrint->setIcon (QIcon (":icons/document-print.svg"));
            ui->actionDoc->setIcon (QIcon (":icons/document-properties.svg"));
            ui->actionUndo->setIcon (QIcon (":icons/edit-undo.svg"));
            ui->actionRedo->setIcon (QIcon (":icons/edit-redo.svg"));
            ui->actionCut->setIcon (QIcon (":icons/edit-cut.svg"));
            ui->actionCopy->setIcon (QIcon (":icons/edit-copy.svg"));
            ui->actionPaste->setIcon (QIcon (":icons/edit-paste.svg"));
            ui->actionDelete->setIcon (QIcon (":icons/edit-delete.svg"));
            ui->actionSelectAll->setIcon (QIcon (":icons/edit-select-all.svg"));
            ui->actionReload->setIcon (QIcon (":icons/view-refresh.svg"));
            ui->actionFind->setIcon (QIcon (":icons/edit-find.svg"));
            ui->actionReplace->setIcon (QIcon (":icons/edit-find-replace.svg"));
            ui->actionClose->setIcon (QIcon (":icons/window-close.svg"));
            ui->actionQuit->setIcon (QIcon (":icons/application-exit.svg"));
            ui->actionFont->setIcon (QIcon (":icons/preferences-desktop-font.svg"));
            ui->actionPreferences->setIcon (QIcon (":icons/preferences-system.svg"));
            ui->actionHelp->setIcon (QIcon (":icons/help-contents.svg"));
            ui->actionAbout->setIcon (QIcon (":icons/help-about.svg"));
            ui->actionJump->setIcon (QIcon (":icons/go-jump.svg"));
            ui->actionEdit->setIcon (QIcon (":icons/document-edit.svg"));
            ui->actionRun->setIcon (QIcon (":icons/system-run.svg"));
            ui->actionCopyName->setIcon (QIcon (":icons/edit-copy.svg"));
            ui->actionCopyPath->setIcon (QIcon (":icons/edit-copy.svg"));

            ui->actionCloseOther->setIcon (QIcon (":icons/tab-close-other.svg"));
            ui->actionMenu->setIcon (QIcon (":icons/application-menu.svg"));

            ui->toolButtonNext->setIcon (QIcon (":icons/go-down.svg"));
            ui->toolButtonPrv->setIcon (QIcon (":icons/go-up.svg"));
            ui->toolButtonAll->setIcon (QIcon (":icons/arrow-down-double.svg"));

            if (rtl)
            {
                ui->actionCloseRight->setIcon (QIcon (":icons/go-previous.svg"));
                ui->actionCloseLeft->setIcon (QIcon (":icons/go-next.svg"));
                ui->actionRightTab->setIcon (QIcon (":icons/go-previous.svg"));
                ui->actionLeftTab->setIcon (QIcon (":icons/go-next.svg"));

                ui->actionRightTab->setShortcut (QKeySequence (tr ("Alt+Left")));
                ui->actionLeftTab->setShortcut (QKeySequence (tr ("Alt+Right")));
            }
            else
            {
                ui->actionCloseRight->setIcon (QIcon (":icons/go-next.svg"));
                ui->actionCloseLeft->setIcon (QIcon (":icons/go-previous.svg"));
                ui->actionRightTab->setIcon (QIcon (":icons/go-next.svg"));
                ui->actionLeftTab->setIcon (QIcon (":icons/go-previous.svg"));
            }

            setWindowIcon (QIcon (":icons/featherpad.svg"));
        }
    }
}
/*************************/
// We want all dialogs to be window-modal as far as possible. However there is a problem:
// If a dialog is opened in a FeatherPad window and is closed after another dialog is
// opened in another window, the second dialog will be seen as a child of the first window.
// This could cause a crash if the dialog is closed after closing the first window.
// As a workaround, we keep window-modality but don't let the user open two window-modal dialogs.
bool FPwin::hasAnotherDialog()
{
    closeWarningBar();
    bool res = false;
    FPsingleton *singleton = static_cast<FPsingleton*>(qApp);
    for (int i = 0; i < singleton->Wins.count(); ++i)
    {
        FPwin *win = singleton->Wins.at (i);
        if (win != this)
        {
            QList<QDialog*> dialogs = win->findChildren<QDialog*>();
            for (int j = 0; j < dialogs.count(); ++j)
            {
                if (dialogs.at (j)->objectName() != "processDialog"
                    && dialogs.at (j)->objectName() != "sessionDialog")
                {
                    res = true;
                    break;
                }
            }
            if (res) break;
        }
    }
    if (res)
    {
        showWarningBar("<center><b><big>" + tr ("Another FeatherPad window has a modal dialog!") + "</big></b></center>"
                       + "<center><i>" +tr ("Please attend to that window or just close its dialog!") + "</i></center>");
    }
    return res;
}
/*************************/
void FPwin::deleteTabPage (int index)
{
    TabPage *tabPage = qobject_cast< TabPage *>(ui->tabWidget->widget (index));
    TextEdit *textEdit = tabPage->textEdit();
    /* because deleting the syntax highlighter changes the text,
       textChanged() signals should be disconnected here to prevent a crash */
    disconnect (textEdit, &QPlainTextEdit::textChanged, this, &FPwin::wordButtonStatus);
    disconnect (textEdit, &QPlainTextEdit::textChanged, this, &FPwin::hlight);
    if (Highlighter *highlighter = qobject_cast< Highlighter *>(textEdit->getHighlighter()))
    {
        disconnect (textEdit, &QPlainTextEdit::cursorPositionChanged, this, &FPwin::matchBrackets);
        disconnect (textEdit, &QPlainTextEdit::blockCountChanged, this, &FPwin::formatOnBlockChange);
        disconnect (textEdit, &TextEdit::updateRect, this, &FPwin::formatVisibleText);
        disconnect (textEdit, &TextEdit::resized, this, &FPwin::formatOnResizing);
        textEdit->setHighlighter (nullptr); // for consistency
        delete highlighter; highlighter = nullptr;
    }
    ui->tabWidget->removeTab (index);
    delete tabPage; tabPage = nullptr;
}
/*************************/
// Here, leftIndx is the tab's index, to whose right all tabs are to be closed.
// Similarly, rightIndx is the tab's index, to whose left all tabs are to be closed.
// A negative value means including the start for leftIndx and the end for rightIndx.
// If both leftIndx and rightIndx are negative, all tabs will be closed.
// The case, when they're both greater than -1, is covered but not used anywhere.
// Tabs are always closed from right to left.
bool FPwin::closeTabs (int leftIndx, int rightIndx)
{
    if (!isReady()) return true;

    bool keep = false;
    int index, count;
    int unsaved = 0;
    while (unsaved == 0 && ui->tabWidget->count() > 0)
    {
        if (QGuiApplication::overrideCursor() == nullptr)
            waitToMakeBusy();

        if (rightIndx == 0) break; // no tab on the left
        if (rightIndx < 0) // close from the end
            index = ui->tabWidget->count() - 1;
        else // if (rightIndx > 0)
            index = rightIndx - 1;

        if (leftIndx >= index)
            break;
        else
            ui->tabWidget->setCurrentIndex (index);
        if (leftIndx == index - 1) // only one tab to be closed
            unsaved = unSaved (index, false);
        else
            unsaved = unSaved (index, true); // with a "No to all" button
        switch (unsaved) {
        case 0: // close this tab and go to the next one on the left
            keep = false;
            deleteTabPage (index);

            if (rightIndx > -1) // also rightIndx > 0
                --rightIndx; // a left tab is removed

            /* final changes */
            count = ui->tabWidget->count();
            if (count == 0)
            {
                ui->actionReload->setDisabled (true);
                ui->actionSave->setDisabled (true);
                enableWidgets (false);
            }
            else if (count == 1)
            {
                ui->actionDetachTab->setDisabled (true);
                ui->actionRightTab->setDisabled (true);
                ui->actionLeftTab->setDisabled (true);
                ui->actionLastTab->setDisabled (true);
                ui->actionFirstTab->setDisabled (true);
            }
            break;
        case 1: // stop quitting (cancel or can't save)
            keep = true;
            break;
        case 2: // no to all: close all tabs (and quit)
            keep = false;
            while (index > leftIndx)
            {
                if (rightIndx == 0) break;
                ui->tabWidget->setCurrentIndex (index);

                deleteTabPage (index);

                if (rightIndx < 0)
                    index = ui->tabWidget->count() - 1;
                else // if (rightIndx > 0)
                {
                    --rightIndx; // a left tab is removed
                    index = rightIndx - 1;
                }

                count = ui->tabWidget->count();
                if (count == 0)
                {
                    ui->actionReload->setDisabled (true);
                    ui->actionSave->setDisabled (true);
                    enableWidgets (false);
                }
                else if (count == 1)
                {
                    ui->actionDetachTab->setDisabled (true);
                    ui->actionRightTab->setDisabled (true);
                    ui->actionLeftTab->setDisabled (true);
                    ui->actionLastTab->setDisabled (true);
                    ui->actionFirstTab->setDisabled (true);
                }
            }
            break;
        default:
            break;
        }
    }

    unbusy();
    return keep;
}
/*************************/
void FPwin::copyTabFileName()
{
    TabPage *tabPage = qobject_cast< TabPage *>(ui->tabWidget->widget (rightClicked_));
    QString fname = tabPage->textEdit()->getFileName();
    QApplication::clipboard()->setText (QFileInfo (fname).fileName());
}
/*************************/
void FPwin::copyTabFilePath()
{
    TabPage *tabPage = qobject_cast< TabPage *>(ui->tabWidget->widget (rightClicked_));
    QString str = tabPage->textEdit()->getFileName();
    str.chop (QFileInfo (str).fileName().count());
    QApplication::clipboard()->setText (str);
}
/*************************/
void FPwin::closeAllTabs()
{
    closeTabs (-1, -1);
}
/*************************/
void FPwin::closeNextTabs()
{
    closeTabs (rightClicked_, -1);
}
/*************************/
void FPwin::closePreviousTabs()
{
    closeTabs (-1, rightClicked_);
}
/*************************/
void FPwin::closeOtherTabs()
{
    if (!closeTabs (rightClicked_, -1))
        closeTabs (-1, rightClicked_);
}
/*************************/
void FPwin::dragEnterEvent (QDragEnterEvent *event)
{
    if (findChildren<QDialog *>().count() == 0
        && (event->mimeData()->hasUrls()
            || event->mimeData()->hasFormat ("application/featherpad-tab")))
    {
        event->acceptProposedAction();
    }
}
/*************************/
void FPwin::dropEvent (QDropEvent *event)
{
    if (event->mimeData()->hasFormat ("application/featherpad-tab"))
        dropTab (event->mimeData()->data("application/featherpad-tab"));
    else
    {
        QList<QUrl> urlList = event->mimeData()->urls();
        bool multiple (urlList.count() > 1 || isLoading());
        foreach (QUrl url, urlList)
            newTabFromName (url.adjusted (QUrl::NormalizePathSegments) // KDE may give a double slash
                               .toLocalFile(),
                            multiple);
    }

    event->acceptProposedAction();
}
/*************************/
// This method checks if there's any text that isn't saved yet.
int FPwin::unSaved (int index, bool noToAll)
{
    int unsaved = 0;
    TextEdit *textEdit = qobject_cast< TabPage *>(ui->tabWidget->widget (index))->textEdit();
    QString fname = textEdit->getFileName();
    if (textEdit->document()->isModified()
        || (!fname.isEmpty() && (!QFile::exists (fname) || !QFileInfo (fname).isFile())))
    {
        unbusy(); // made busy at closeTabs()
        if (hasAnotherDialog()) return 1; // cancel
        disableShortcuts (true);

        MessageBox msgBox (this);
        msgBox.setIcon (QMessageBox::Question);
        msgBox.setText ("<center><b><big>" + tr ("Save changes?") + "</big></b></center>");
        if (textEdit->document()->isModified())
            msgBox.setInformativeText ("<center><i>" + tr ("The document has been modified.") + "</i></center>");
        else
            msgBox.setInformativeText ("<center><i>" + tr ("The file has been removed.") + "</i></center>");
        if (noToAll && ui->tabWidget->count() > 1)
            msgBox.setStandardButtons (QMessageBox::Save
                                       | QMessageBox::Discard
                                       | QMessageBox::Cancel
                                       | QMessageBox::NoToAll);
        else
            msgBox.setStandardButtons (QMessageBox::Save
                                       | QMessageBox::Discard
                                       | QMessageBox::Cancel);
        msgBox.changeButtonText (QMessageBox::Save, tr ("Save"));
        msgBox.changeButtonText (QMessageBox::Discard, tr ("Discard changes"));
        msgBox.changeButtonText (QMessageBox::Cancel, tr ("Cancel"));
        if (noToAll)
            msgBox.changeButtonText (QMessageBox::NoToAll, tr ("No to all"));
        msgBox.setDefaultButton (QMessageBox::Save);
        msgBox.setWindowModality (Qt::WindowModal);
        /* enforce a central position */
        /*msgBox.show();
        msgBox.move (x() + width()/2 - msgBox.width()/2,
                     y() + height()/2 - msgBox.height()/ 2);*/
        switch (msgBox.exec()) {
        case QMessageBox::Save:
            if (!saveFile (true))
                unsaved = 1;
            break;
        case QMessageBox::Discard:
            break;
        case QMessageBox::Cancel:
            unsaved = 1;
            break;
        case QMessageBox::NoToAll:
            unsaved = 2;
            break;
        default:
            unsaved = 1;
            break;
        }
        disableShortcuts (false);
    }
    return unsaved;
}
/*************************/
// Enable or disable some widgets.
void FPwin::enableWidgets (bool enable) const
{
    if (!enable && ui->dockReplace->isVisible())
        ui->dockReplace->setVisible (false);
    if (!enable && ui->spinBox->isVisible())
    {
        ui->spinBox->setVisible (false);
        ui->label->setVisible (false);
        ui->checkBox->setVisible (false);
    }
    if ((!enable && ui->statusBar->isVisible())
        || (enable
            && static_cast<FPsingleton*>(qApp)->getConfig()
               .getShowStatusbar())) // starting from no tab
    {
        ui->statusBar->setVisible (enable);
    }

    ui->actionSelectAll->setEnabled (enable);
    ui->actionFind->setEnabled (enable);
    ui->actionJump->setEnabled (enable);
    ui->actionReplace->setEnabled (enable);
    ui->actionClose->setEnabled (enable);
    ui->actionSaveAs->setEnabled (enable);
    ui->menuEncoding->setEnabled (enable);
    ui->actionSaveCodec->setEnabled (enable);
    ui->actionFont->setEnabled (enable);
    ui->actionDoc->setEnabled (enable);
    ui->actionPrint->setEnabled (enable);

    if (!enable)
    {
        ui->actionUndo->setEnabled (false);
        ui->actionRedo->setEnabled (false);

        ui->actionEdit->setVisible (false);
        ui->actionRun->setVisible (false);

        ui->actionCut->setEnabled (false);
        ui->actionCopy->setEnabled (false);
        ui->actionPaste->setEnabled (false);
        ui->actionDelete->setEnabled (false);
    }
}
/*************************/
// When a window-modal dialog is shown, Qt doesn't disable the main window shortcuts.
// This is definitely a bug in Qt. As a workaround, we use this function to disable
// all shortcuts on showing a dialog and to enable them again on hiding it.
// The searchbar shortcuts of the current tab page are handled separately.
void FPwin::disableShortcuts (bool disable, bool page)
{
    if (disable)
    {
        ui->actionLineNumbers->setShortcut (QKeySequence());
        ui->actionWrap->setShortcut (QKeySequence());
        ui->actionIndent->setShortcut (QKeySequence());
        ui->actionSyntax->setShortcut (QKeySequence());

        ui->actionNew->setShortcut (QKeySequence());
        ui->actionOpen->setShortcut (QKeySequence());
        ui->actionSave->setShortcut (QKeySequence());
        ui->actionFind->setShortcut (QKeySequence());
        ui->actionReplace->setShortcut (QKeySequence());
        ui->actionSaveAs->setShortcut (QKeySequence());
        ui->actionPrint->setShortcut (QKeySequence());
        ui->actionDoc->setShortcut (QKeySequence());
        ui->actionClose->setShortcut (QKeySequence());
        ui->actionQuit->setShortcut (QKeySequence());
        ui->actionPreferences->setShortcut (QKeySequence());
        ui->actionHelp->setShortcut (QKeySequence());
        ui->actionEdit->setShortcut (QKeySequence());
        ui->actionDetachTab->setShortcut (QKeySequence());
        ui->actionReload->setShortcut (QKeySequence());

        ui->actionUndo->setShortcut (QKeySequence());
        ui->actionRedo->setShortcut (QKeySequence());
        ui->actionCut->setShortcut (QKeySequence());
        ui->actionCopy->setShortcut (QKeySequence());
        ui->actionPaste->setShortcut (QKeySequence());
        ui->actionSelectAll->setShortcut (QKeySequence());

        ui->toolButtonNext->setShortcut (QKeySequence());
        ui->toolButtonPrv->setShortcut (QKeySequence());
        ui->toolButtonAll->setShortcut (QKeySequence());

        ui->actionRightTab->setShortcut (QKeySequence());
        ui->actionLeftTab->setShortcut (QKeySequence());
        ui->actionLastTab->setShortcut (QKeySequence());
        ui->actionFirstTab->setShortcut (QKeySequence());
    }
    else
    {
        ui->actionLineNumbers->setShortcut (QKeySequence (tr ("Ctrl+L")));
        ui->actionWrap->setShortcut (QKeySequence (tr ("Ctrl+W")));
        ui->actionIndent->setShortcut (QKeySequence (tr ("Ctrl+I")));
        ui->actionSyntax->setShortcut (QKeySequence (tr ("Ctrl+Shift+H")));

        ui->actionNew->setShortcut (QKeySequence (tr ("Ctrl+N")));
        ui->actionOpen->setShortcut (QKeySequence (tr ("Ctrl+O")));
        ui->actionSave->setShortcut (QKeySequence (tr ("Ctrl+S")));
        ui->actionFind->setShortcut (QKeySequence (tr ("Ctrl+F")));
        ui->actionReplace->setShortcut (QKeySequence (tr ("Ctrl+R")));
        ui->actionSaveAs->setShortcut (QKeySequence (tr ("Ctrl+Shift+S")));
        ui->actionPrint->setShortcut (QKeySequence (tr ("Ctrl+P")));
        ui->actionDoc->setShortcut (QKeySequence (tr ("Ctrl+Shift+D")));
        ui->actionClose->setShortcut (QKeySequence (tr ("Ctrl+Shift+Q")));
        ui->actionQuit->setShortcut (QKeySequence (tr ("Ctrl+Q")));
        ui->actionPreferences->setShortcut (QKeySequence (tr ("Ctrl+Shift+P")));
        ui->actionHelp->setShortcut (QKeySequence (tr ("Ctrl+H")));
        ui->actionEdit->setShortcut (QKeySequence (tr ("Ctrl+E")));
        ui->actionDetachTab->setShortcut (QKeySequence (tr ("Ctrl+T")));
        ui->actionReload->setShortcut (QKeySequence (tr ("Ctrl+Shift+R")));

        ui->actionUndo->setShortcut (QKeySequence (tr ("Ctrl+Z")));
        ui->actionRedo->setShortcut (QKeySequence (tr ("Ctrl+Shift+Z")));
        ui->actionCut->setShortcut (QKeySequence (tr ("Ctrl+X")));
        ui->actionCopy->setShortcut (QKeySequence (tr ("Ctrl+C")));
        ui->actionPaste->setShortcut (QKeySequence (tr ("Ctrl+V")));
        ui->actionSelectAll->setShortcut (QKeySequence (tr ("Ctrl+A")));

        ui->toolButtonNext->setShortcut (QKeySequence (tr ("F7")));
        ui->toolButtonPrv->setShortcut (QKeySequence (tr ("F8")));
        ui->toolButtonAll->setShortcut (QKeySequence (tr ("F9")));

        if (QApplication::layoutDirection() == Qt::RightToLeft)
        {
            ui->actionRightTab->setShortcut (QKeySequence (tr ("Alt+Left")));
            ui->actionLeftTab->setShortcut (QKeySequence (tr ("Alt+Right")));
        }
        else
        {
            ui->actionRightTab->setShortcut (QKeySequence (tr ("Alt+Right")));
            ui->actionLeftTab->setShortcut (QKeySequence (tr ("Alt+Left")));
        }
        ui->actionLastTab->setShortcut (QKeySequence (tr ("Alt+Up")));
        ui->actionFirstTab->setShortcut (QKeySequence (tr ("Alt+Down")));
    }
    if (page) // disable/enable searchbar shortcuts of the current page too
    {
        if (TabPage *tabPage = qobject_cast< TabPage *>(ui->tabWidget->currentWidget()))
            tabPage->disableShortcuts (disable);
    }
}
/*************************/
void FPwin::newTab()
{
    createEmptyTab (!isLoading());
}
/*************************/
TabPage* FPwin::createEmptyTab (bool setCurrent)
{
    Config config = static_cast<FPsingleton*>(qApp)->getConfig();

    TabPage *tabPage = new TabPage (iconMode_,
                                    config.getDarkColScheme() ? config.getDarkBgColorValue()
                                                              : config.getLightBgColorValue(),
                                    nullptr);
    TextEdit *textEdit = tabPage->textEdit();
    textEdit->setScrollJumpWorkaround (config.getScrollJumpWorkaround());
    textEdit->document()->setDefaultFont (config.getFont());
    /* we want consistent tabs */
    QFontMetrics metrics (config.getFont());
    textEdit->setTabStopWidth (4 * metrics.width (' '));

    int index = ui->tabWidget->currentIndex();
    if (index == -1) enableWidgets (true);

    /* hide the searchbar consistently */
    if ((index == -1 && config.getHideSearchbar())
        || (index > -1 && !qobject_cast< TabPage *>(ui->tabWidget->widget (index))->isSearchBarVisible()))
    {
        tabPage->setSearchBarVisible (false);
    }

    ui->tabWidget->insertTab (index + 1, tabPage, tr ("Untitled"));

    /* set all preliminary properties */
    if (index >= 0)
    {
        ui->actionDetachTab->setEnabled (true);
        ui->actionRightTab->setEnabled (true);
        ui->actionLeftTab->setEnabled (true);
        ui->actionLastTab->setEnabled (true);
        ui->actionFirstTab->setEnabled (true);
    }
    ui->tabWidget->setTabToolTip (index + 1, tr ("Unsaved"));
    if (!ui->actionWrap->isChecked())
        textEdit->setLineWrapMode (QPlainTextEdit::NoWrap);
    if (!ui->actionIndent->isChecked())
        textEdit->setAutoIndentation (false);
    if (ui->actionLineNumbers->isChecked() || ui->spinBox->isVisible())
        textEdit->showLineNumbers (true);
    if (ui->spinBox->isVisible())
        connect (textEdit->document(), &QTextDocument::blockCountChanged, this, &FPwin::setMax);
    if (ui->statusBar->isVisible()
        || config.getShowStatusbar()) // when the main window is being created, isVisible() isn't set yet
    {
        if (setCurrent)
        {
            if (QToolButton *wordButton = ui->statusBar->findChild<QToolButton *>())
                wordButton->setVisible (false);
            QLabel *statusLabel = ui->statusBar->findChild<QLabel *>();
            statusLabel->setText ("<b>" + tr ("Encoding") + ":</b> <i>UTF-8</i>&nbsp;&nbsp;&nbsp;&nbsp;<b>"
                                        + tr ("Lines") + ":</b> <i>1</i>&nbsp;&nbsp;&nbsp;&nbsp;<b>"
                                        + tr ("Sel. Chars") + ":</b> <i>0</i>&nbsp;&nbsp;&nbsp;&nbsp;<b>"
                                        + tr ("Words") + ":</b> <i>0</i>");
        }
        connect (textEdit, &QPlainTextEdit::blockCountChanged, this, &FPwin::statusMsgWithLineCount);
        connect (textEdit, &QPlainTextEdit::selectionChanged, this, &FPwin::statusMsg);
    }
    connect (textEdit->document(), &QTextDocument::undoAvailable, ui->actionUndo, &QAction::setEnabled);
    connect (textEdit->document(), &QTextDocument::redoAvailable, ui->actionRedo, &QAction::setEnabled);
    connect (textEdit->document(), &QTextDocument::modificationChanged, ui->actionSave, &QAction::setEnabled);
    connect (textEdit->document(), &QTextDocument::modificationChanged, this, &FPwin::asterisk);
    connect (textEdit, &QPlainTextEdit::copyAvailable, ui->actionCut, &QAction::setEnabled);
    connect (textEdit, &QPlainTextEdit::copyAvailable, ui->actionDelete, &QAction::setEnabled);
    connect (textEdit, &QPlainTextEdit::copyAvailable, ui->actionCopy, &QAction::setEnabled);
    connect (textEdit, &TextEdit::fileDropped, this, &FPwin::newTabFromName);
    connect (textEdit, &TextEdit::zoomedOut, this, &FPwin::reformat);

    connect (tabPage, &TabPage::find, this, &FPwin::find);
    connect (tabPage, &TabPage::searchFlagChanged, this, &FPwin::searchFlagChanged);

    /* I don't know why, under KDE, when text is selected
       for the first time, it isn't copied to the selection
       clipboard. Perhaps it has something to do with Klipper.
       I neither know why this s a workaround: */
    QApplication::clipboard()->text (QClipboard::Selection);

    if (setCurrent)
    {
        ui->tabWidget->setCurrentWidget (tabPage);
        textEdit->setFocus();
    }

    /* this isn't enough for unshading under all WMs */
    /*if (isMinimized())
        setWindowState (windowState() & (~Qt::WindowMinimized | Qt::WindowActive));*/
    if (static_cast<FPsingleton*>(qApp)->isX11() && isWindowShaded (winId()))
        unshadeWindow (winId());
    activateWindow();
    raise();

    return tabPage;
}
/*************************/
void FPwin::updateRecenMenu()
{
    Config config = static_cast<FPsingleton*>(qApp)->getConfig();
    QStringList recentFiles = config.getRecentFiles();
    int recentNumber = config.getCurRecentFilesNumber();

    QList<QAction *> actions = ui->menuOpenRecently->actions();
    int recentSize = recentFiles.count();
    QFontMetrics metrics (ui->menuOpenRecently->font());
    int w = 150 * metrics.width (' ');
    for (int i = 0; i < recentNumber; ++i)
    {
        if (i < recentSize)
        {
            actions.at (i)->setText (metrics.elidedText (recentFiles.at (i), Qt::ElideMiddle, w));
            actions.at (i)->setData (recentFiles.at (i));
            actions.at (i)->setVisible (true);
        }
        else
        {
            actions.at (i)->setText (QString());
            actions.at (i)->setData (QVariant());
            actions.at (i)->setVisible (false);
        }
    }
    ui->actionClearRecent->setEnabled (recentSize != 0);
}
/*************************/
void FPwin::clearRecentMenu()
{
    Config& config = static_cast<FPsingleton*>(qApp)->getConfig();
    config.clearRecentFiles();
    updateRecenMenu();
}
/*************************/
void FPwin::reformat (TextEdit *textEdit)
{
    formatTextRect (textEdit->rect()); // in "syntax.cpp"
    if (!textEdit->getSearchedText().isEmpty())
        hlight(); // in "find.cpp"
}
/*************************/
void FPwin::zoomIn()
{
    int index = ui->tabWidget->currentIndex();
    if (index == -1) return;
    TextEdit *textEdit = qobject_cast< TabPage *>(ui->tabWidget->widget (index))->textEdit();
    textEdit->zooming (1.f);

    /* this is sometimes needed for the
       scrollbar range(s) to be updated (a Qt bug?) */
    //QTimer::singleShot (0, textEdit, SLOT (updateEditorGeometry()));
}
/*************************/
void FPwin::zoomOut()
{
    int index = ui->tabWidget->currentIndex();
    if (index == -1) return;
    TextEdit *textEdit = qobject_cast< TabPage *>(ui->tabWidget->widget (index))->textEdit();
    textEdit->zooming (-1.f);

    reformat (textEdit);
}
/*************************/
void FPwin::zoomZero()
{
    int index = ui->tabWidget->currentIndex();
    if (index == -1) return;
    Config config = static_cast<FPsingleton*>(qApp)->getConfig();
    TextEdit *textEdit = qobject_cast< TabPage *>(ui->tabWidget->widget (index))->textEdit();
    textEdit->setFont (config.getFont());
    QFontMetrics metrics (config.getFont());
    textEdit->setTabStopWidth (4 * metrics.width (' '));

    //QTimer::singleShot (0, textEdit, SLOT (updateEditorGeometry()));

    /* this may be a zoom-out */
    reformat (textEdit);
}
/*************************/
void FPwin::fullScreening()
{
    setWindowState (windowState() ^ Qt::WindowFullScreen);
}
/*************************/
void FPwin::defaultSize()
{
    if (size() == QSize (700, 500)) return;
    if (isMaximized() && isFullScreen())
        showMaximized();
    if (isMaximized())
        showNormal();
    /* instead of hiding, reparent with the dummy
       widget to guarantee resizing under all DEs */
    setParent (dummyWidget, Qt::SubWindow);
    resize (static_cast<FPsingleton*>(qApp)->getConfig().getStartSize());
    if (parent() != nullptr)
        setParent (nullptr, Qt::Window);
    QTimer::singleShot (0, this, SLOT (show()));
}
/*************************/
void FPwin::align()
{
    int index = ui->tabWidget->currentIndex();
    if (index == -1) return;

    TextEdit *textEdit = qobject_cast< TabPage *>(ui->tabWidget->widget (index))->textEdit();
    QTextOption opt = textEdit->document()->defaultTextOption();
    if (opt.alignment() == (Qt::AlignLeft))
    {
        opt = QTextOption (Qt::AlignRight);
        opt.setTextDirection (Qt::LayoutDirectionAuto);
        textEdit->document()->setDefaultTextOption (opt);
    }
    else if (opt.alignment() == (Qt::AlignRight))
    {
        opt = QTextOption (Qt::AlignLeft);
        opt.setTextDirection (Qt::LayoutDirectionAuto);
        textEdit->document()->setDefaultTextOption (opt);
    }
}
/*************************/
void FPwin::executeProcess()
{
    QList<QDialog*> dialogs = findChildren<QDialog*>();
    for (int i = 0; i < dialogs.count(); ++i)
    {
        if (dialogs.at (i)->objectName() != "processDialog"
            && dialogs.at (i)->objectName() != "sessionDialog")
        {
            return; // shortcut may work when there's a modal dialog
        }
    }
    closeWarningBar();

    Config config = static_cast<FPsingleton*>(qApp)->getConfig();
    if (!config.getExecuteScripts()) return;

    int index = ui->tabWidget->currentIndex();
    if (index == -1) return;
    TabPage *tabPage = qobject_cast< TabPage *>(ui->tabWidget->widget (index));

    if (tabPage->findChild<QProcess *>(QString(), Qt::FindDirectChildrenOnly))
    {
        showWarningBar ("<center><b><big>" + tr ("Another process is running in this tab!") + "</big></b></center>"
                        + "<center><i>" + tr ("Only one process is allowed per tab.") + "</i></center>");
        return;
    }

    QString fName = tabPage->textEdit()->getFileName();
    if (!isScriptLang (tabPage->textEdit()->getProg())  || !QFileInfo (fName).isExecutable())
        return;

    QProcess *process = new QProcess (tabPage);
    process->setObjectName (fName); // to put it into the message dialog
    connect(process, &QProcess::readyReadStandardOutput,this, &FPwin::displayOutput);
    connect(process, &QProcess::readyReadStandardError,this, &FPwin::displayError);
    QString command = config.getExecuteCommand();
    if (!command.isEmpty())
        command +=  " ";
    fName.replace ("\"", "\"\"\""); // literal quotes in the command are shown by triple quotes
    process->start (command + "\"" + fName + "\"");
    connect(process, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            [=](int /*exitCode*/, QProcess::ExitStatus /*exitStatus*/){ process->deleteLater(); });
}
/*************************/
bool FPwin::isScriptLang (QString lang)
{
    return (lang == "sh" || lang == "python"
            || lang == "ruby" || lang == "lua"
            || lang == "perl");
}
/*************************/
void FPwin::exitProcess()
{
    int index = ui->tabWidget->currentIndex();
    if (index == -1) return;
    TabPage *tabPage = qobject_cast< TabPage *>(ui->tabWidget->widget (index));
    if (QProcess *process = tabPage->findChild<QProcess *>(QString(), Qt::FindDirectChildrenOnly))
        process->kill();
}
/*************************/
void FPwin::displayMessage (bool error)
{
    QProcess *process = static_cast<QProcess*>(QObject::sender());
    if (!process) return; // impossible
    QByteArray msg;
    if (error)
    {
        process->setReadChannel(QProcess::StandardError);
        msg = process->readAllStandardError();
    }
    else
    {
        process->setReadChannel(QProcess::StandardOutput);
        msg = process->readAllStandardOutput();
    }
    if (msg.isEmpty()) return;

    QPointer<QDialog> msgDlg = nullptr;
    QList<QDialog*> dialogs = findChildren<QDialog*>();
    for (int i = 0; i < dialogs.count(); ++i)
    {
        if (dialogs.at (i)->parent() == process->parent())
        {
            msgDlg = dialogs.at (i);
            break;
        }
    }
    if (msgDlg)
    { // append to the existing message
        if (QPlainTextEdit *tEdit = msgDlg->findChild<QPlainTextEdit*>())
        {
            tEdit->setPlainText (tEdit->toPlainText() + "\n" + msg.constData());
            QTextCursor cur = tEdit->textCursor();
            cur.movePosition (QTextCursor::End);
            tEdit->setTextCursor (cur);
        }
    }
    else
    {
        msgDlg = new QDialog (qobject_cast<QWidget*>(process->parent()));
        msgDlg->setObjectName ("processDialog");
        msgDlg->setWindowTitle (tr ("Script Output"));
        msgDlg->setSizeGripEnabled (true);
        QGridLayout *grid = new QGridLayout;
        QLabel *label = new QLabel (msgDlg);
        label->setText ("<center><b>" + tr ("Script File") + ": </b></center><i>" + process->objectName() + "</i>");
        label->setTextInteractionFlags (Qt::TextSelectableByMouse);
        label->setWordWrap (true);
        label->setMargin (5);
        grid->addWidget (label, 0, 0, 1, 2);
        QPlainTextEdit *tEdit = new QPlainTextEdit (msgDlg);
        tEdit->setTextInteractionFlags (Qt::TextSelectableByMouse);
        tEdit->ensureCursorVisible();
        grid->addWidget (tEdit, 1, 0, 1, 2);
        QPushButton *closeButton = new QPushButton (QIcon::fromTheme ("edit-delete"), tr ("Close"));
        connect (closeButton, &QAbstractButton::clicked, msgDlg, &QDialog::reject);
        grid->addWidget (closeButton, 2, 1, Qt::AlignRight);
        QPushButton *clearButton = new QPushButton (QIcon::fromTheme ("edit-clear"), tr ("Clear"));
        connect (clearButton, &QAbstractButton::clicked, tEdit, &QPlainTextEdit::clear);
        grid->addWidget (clearButton, 2, 0, Qt::AlignLeft);
        msgDlg->setLayout (grid);
        tEdit->setPlainText (msg.constData());
        QTextCursor cur = tEdit->textCursor();
        cur.movePosition (QTextCursor::End);
        tEdit->setTextCursor (cur);
        msgDlg->setAttribute (Qt::WA_DeleteOnClose);
        msgDlg->show();
        msgDlg->raise();
        msgDlg->activateWindow();
    }
}
/*************************/
void FPwin::displayOutput()
{
    displayMessage (false);
}
/*************************/
void FPwin::displayError()
{
    displayMessage (true);
}
/*************************/
void FPwin::closeTab()
{
    if (!isReady()) return;

    int index = ui->tabWidget->currentIndex();
    if (index == -1) return; // not needed

    if (unSaved (index, false)) return;

    deleteTabPage (index);
    int count = ui->tabWidget->count();
    if (count == 0)
    {
        ui->actionReload->setDisabled (true);
        ui->actionSave->setDisabled (true);
        enableWidgets (false);
    }
    else // set focus to text-edit
    {
        if (count == 1)
        {
            ui->actionDetachTab->setDisabled (true);
            ui->actionRightTab->setDisabled (true);
            ui->actionLeftTab->setDisabled (true);
            ui->actionLastTab->setDisabled (true);
            ui->actionFirstTab->setDisabled (true);
        }
        if (TabPage *tabPage = qobject_cast< TabPage *>(ui->tabWidget->currentWidget()))
            tabPage->textEdit()->setFocus();
    }
}
/*************************/
void FPwin::closeTabAtIndex (int index)
{
    if (unSaved (index, false)) return;
    closeWarningBar();

    deleteTabPage (index);
    int count = ui->tabWidget->count();
    if (count == 0)
    {
        ui->actionReload->setDisabled (true);
        ui->actionSave->setDisabled (true);
        enableWidgets (false);
    }
    else
    {
        if (count == 1)
        {
            ui->actionDetachTab->setDisabled (true);
            ui->actionRightTab->setDisabled (true);
            ui->actionLeftTab->setDisabled (true);
            ui->actionLastTab->setDisabled (true);
            ui->actionFirstTab->setDisabled (true);
        }
        if (TabPage *tabPage = qobject_cast< TabPage *>(ui->tabWidget->currentWidget()))
            tabPage->textEdit()->setFocus();
    }
}
/*************************/
void FPwin::setTitle (const QString& fileName, int indx)
{
    int index = indx;
    if (index < 0)
        index = ui->tabWidget->currentIndex(); // is never -1

    QString shownName;
    if (fileName.isEmpty())
        shownName = tr ("Untitled");
    else
    {
        shownName = QFileInfo (fileName).fileName();
        shownName.replace ("\n", " "); // no multi-line tab text
    }

    if (indx < 0)
        setWindowTitle (shownName);

    shownName.replace ("&", "&&"); // single ampersand is for mnemonic
    ui->tabWidget->setTabText (index, shownName);
}
/*************************/
void FPwin::asterisk (bool modified)
{
    int index = ui->tabWidget->currentIndex();

    QString fname = qobject_cast< TabPage *>(ui->tabWidget->widget (index))
                    ->textEdit()->getFileName();
    QString shownName;
    if (modified)
    {
        if (fname.isEmpty())
            shownName = tr ("*Untitled");
        else
            shownName = QFileInfo (fname).fileName().prepend("*");
    }
    else
    {
        if (fname.isEmpty())
            shownName = tr ("Untitled");
        else
            shownName = QFileInfo (fname).fileName();
    }
    shownName.replace ("\n", " ");

    setWindowTitle (shownName);

    shownName.replace ("&", "&&");
    ui->tabWidget->setTabText (index, shownName);
}
/*************************/
void FPwin::waitToMakeBusy()
{
    if (busyThread_ != nullptr) return;

    busyThread_ = new QThread;
    BusyMaker *makeBusy = new BusyMaker();
    makeBusy->moveToThread(busyThread_);
    connect (busyThread_, &QThread::started, makeBusy, &BusyMaker::waiting);
    connect (busyThread_, &QThread::finished, busyThread_, &QObject::deleteLater);
    connect (makeBusy, &BusyMaker::finished, busyThread_, &QThread::quit);
    connect (makeBusy, &BusyMaker::finished, makeBusy, &QObject::deleteLater);
    busyThread_->start();
}
/*************************/
void FPwin::unbusy()
{
    if (busyThread_ && !busyThread_->isFinished())
    {
        busyThread_->quit();
        busyThread_->wait();
    }
    if (QGuiApplication::overrideCursor() != nullptr)
        QGuiApplication::restoreOverrideCursor();
}
/*************************/
void FPwin::loadText (const QString fileName, bool enforceEncod, bool reload, bool multiple)
{
    if (loadingProcesses_ == 0)
        closeWarningBar();
    ++ loadingProcesses_;
    QString charset;
    if (enforceEncod)
        charset = checkToEncoding();
    Loading *thread = new Loading (fileName, charset, reload, multiple);
    connect (thread, &Loading::completed, this, &FPwin::addText);
    connect (thread, &Loading::finished, thread, &QObject::deleteLater);
    thread->start();

    if (QGuiApplication::overrideCursor() == nullptr)
        waitToMakeBusy();
    ui->tabWidget->tabBar()->lockTabs (true);
    disableShortcuts (true, false);
}
/*************************/
// When multiple files are being loaded, we don't change the current tab.
void FPwin::addText (const QString text, const QString fileName, const QString charset,
                     bool enforceEncod, bool reload, bool multiple)
{
    if (fileName.isEmpty() || charset.isEmpty())
    {
        if (!fileName.isEmpty() && charset.isEmpty()) // means a very large file
            connect (this, &FPwin::finishedLoading, this, &FPwin::onOpeningHugeFiles, Qt::UniqueConnection);
        -- loadingProcesses_; // can never become negative
        if (!isLoading())
        {
            unbusy();
            ui->tabWidget->tabBar()->lockTabs (false);
            disableShortcuts (false, false);
            emit finishedLoading();
        }
        return;
    }

    if (enforceEncod || reload)
        multiple = false; // respect the logic

    TextEdit *textEdit;
    TabPage *tabPage;
    if (ui->tabWidget->currentIndex() == -1)
        tabPage = createEmptyTab (!multiple);
    else
        tabPage = qobject_cast< TabPage *>(ui->tabWidget->currentWidget());
    textEdit = tabPage->textEdit();

    bool openInCurrentTab (true);
    if (!reload
        && !enforceEncod
        && (!textEdit->document()->isEmpty()
            || textEdit->document()->isModified()
            || !textEdit->getFileName().isEmpty()))
    {
        tabPage = createEmptyTab (!multiple);
        textEdit = tabPage->textEdit();
        openInCurrentTab = false;
    }
    else
    {
        /*if (isMinimized())
            setWindowState (windowState() & (~Qt::WindowMinimized | Qt::WindowActive));*/
        if (static_cast<FPsingleton*>(qApp)->isX11() && isWindowShaded (winId()))
            unshadeWindow (winId());
        activateWindow();
        raise();
    }

    /* this is a workaround for the RTL bug in QPlainTextEdit */
    QTextOption opt = textEdit->document()->defaultTextOption();
    if (text.isRightToLeft())
    {
        if (opt.alignment() == (Qt::AlignLeft))
        {
            opt = QTextOption (Qt::AlignRight);
            opt.setTextDirection (Qt::LayoutDirectionAuto);
            textEdit->document()->setDefaultTextOption (opt);
        }
    }
    else if (opt.alignment() == (Qt::AlignRight))
    {
        opt = QTextOption (Qt::AlignLeft);
        opt.setTextDirection (Qt::LayoutDirectionAuto);
        textEdit->document()->setDefaultTextOption (opt);
    }

    /* we want to restore the cursor later */
    int pos = 0, anchor = 0;
    if (reload)
    {
        pos = textEdit->textCursor().position();
        anchor = textEdit->textCursor().anchor();
    }

    /* set the text */
    disconnect (textEdit->document(), &QTextDocument::modificationChanged, ui->actionSave, &QAction::setEnabled);
    disconnect (textEdit->document(), &QTextDocument::modificationChanged, this, &FPwin::asterisk);
    textEdit->setPlainText (text);
    connect (textEdit->document(), &QTextDocument::modificationChanged, ui->actionSave, &QAction::setEnabled);
    connect (textEdit->document(), &QTextDocument::modificationChanged, this, &FPwin::asterisk);

    /* now, restore the cursor */
    if (reload)
    {
        QTextCursor cur = textEdit->textCursor();
        cur.movePosition (QTextCursor::End, QTextCursor::MoveAnchor);
        int curPos = cur.position();
        if (anchor <= curPos && pos <= curPos)
        {
            cur.setPosition (anchor);
            cur.setPosition (pos, QTextCursor::KeepAnchor);
        }
        textEdit->setTextCursor (cur);
    }

    if (enforceEncod || reload)
    { // uninstall the syntax highlgihter to reinstall it below
        textEdit->setGreenSel (QList<QTextEdit::ExtraSelection>()); // they'll have no meaning later
        if (Highlighter *highlighter = qobject_cast< Highlighter *>(textEdit->getHighlighter()))
        {
            disconnect (textEdit, &QPlainTextEdit::cursorPositionChanged, this, &FPwin::matchBrackets);
            disconnect (textEdit, &QPlainTextEdit::blockCountChanged, this, &FPwin::formatOnBlockChange);
            disconnect (textEdit, &TextEdit::updateRect, this, &FPwin::formatVisibleText);
            disconnect (textEdit, &TextEdit::resized, this, &FPwin::formatOnResizing);

            /* remove bracket highlights to recreate them only if needed */
            QList<QTextEdit::ExtraSelection> es = textEdit->extraSelections();
            int n = textEdit->getRedSel().count();
            while (n > 0 && !es.isEmpty())
            {
                es.removeLast();
                --n;
            }
            textEdit->setRedSel (QList<QTextEdit::ExtraSelection>());
            textEdit->setExtraSelections (es);

            textEdit->setHighlighter (nullptr);
            delete highlighter; highlighter = nullptr;
        }
    }

    QFileInfo fInfo (fileName);
    Config& config = static_cast<FPsingleton*>(qApp)->getConfig();

    textEdit->setFileName (fileName);
    textEdit->setSize (fInfo.size());
    lastFile_ = fileName;
    if (config.getRecentOpened())
        config.addRecentFile (lastFile_);
    textEdit->setEncoding (charset);
    textEdit->setWordNumber (-1);
    setProgLang (textEdit);
    if (ui->actionSyntax->isChecked())
        syntaxHighlighting (textEdit);
    setTitle (fileName, (multiple && !openInCurrentTab) ?
                        /* the index may have changed because syntaxHighlighting() waits for
                           all events to be processed (but it won't change from here on) */
                        ui->tabWidget->indexOf (tabPage) : -1);
    QString tip (fInfo.absolutePath() + "/");
    QFontMetrics metrics (QToolTip::font());
    int w = QApplication::desktop()->screenGeometry().width();
    if (w > 200 * metrics.width (' ')) w = 200 * metrics.width (' ');
    QString elidedTip = metrics.elidedText (tip, Qt::ElideMiddle, w);
    ui->tabWidget->setTabToolTip (ui->tabWidget->indexOf (tabPage), elidedTip);

    if (alreadyOpen (tabPage))
    {
        textEdit->setReadOnly (true);
        if (!textEdit->hasDarkScheme())
            textEdit->viewport()->setStyleSheet (".QWidget {"
                                                 "color: black;"
                                                 "background-color: rgb(236, 236, 208);}");
        else
            textEdit->viewport()->setStyleSheet (".QWidget {"
                                                 "color: white;"
                                                 "background-color: rgb(60, 0, 0);}");
        if (!multiple || openInCurrentTab)
        {
            ui->actionEdit->setVisible (true);
            ui->actionCut->setDisabled (true);
            ui->actionPaste->setDisabled (true);
            ui->actionDelete->setDisabled (true);
        }
        disconnect (textEdit, &QPlainTextEdit::copyAvailable, ui->actionCut, &QAction::setEnabled);
        disconnect (textEdit, &QPlainTextEdit::copyAvailable, ui->actionDelete, &QAction::setEnabled);
    }
    else if (textEdit->isReadOnly())
        QTimer::singleShot (0, this, SLOT (makeEditable()));

    if (!multiple || openInCurrentTab)
    {
        if (ui->statusBar->isVisible())
        {
            statusMsgWithLineCount (textEdit->document()->blockCount());
            if (QToolButton *wordButton = ui->statusBar->findChild<QToolButton *>())
                wordButton->setVisible (true);
        }
        encodingToCheck (charset);
        ui->actionReload->setEnabled (true);
        textEdit->setFocus(); // the text may have been opened in this (empty) tab

        if (openInCurrentTab)
        {
            if (isScriptLang (textEdit->getProg()) && fInfo.isExecutable())
                ui->actionRun->setVisible (config.getExecuteScripts());
            else
                ui->actionRun->setVisible (false);
        }
    }

    /* a file is completely loaded */
    -- loadingProcesses_;
    if (!isLoading())
    {
        unbusy();
        ui->tabWidget->tabBar()->lockTabs (false);
        disableShortcuts (false, false);
        emit finishedLoading();
    }
}
/*************************/
void FPwin::onOpeningHugeFiles()
{
    disconnect (this, &FPwin::finishedLoading, this, &FPwin::onOpeningHugeFiles);
    showWarningBar ("<center><b><big>" + tr ("Huge file(s) not opened!") + "</big></b></center>\n"
                    + "<center>" + tr ("FeatherPad does not open files larger than 500 MiB.") + "</center>");
}
/*************************/
void FPwin::showWarningBar (const QString& message)
{
    closeWarningBar();

    WarningBar *bar = new WarningBar (message, iconMode_);
    ui->verticalLayout->insertWidget (2, bar);
    connect (bar, &WarningBar::closeButtonPressed, [=]{ui->verticalLayout->removeWidget(bar); bar->deleteLater();});
}
/*************************/
void FPwin::closeWarningBar()
{
    if (QLayoutItem *item = ui->verticalLayout->itemAt (2))
    {
        if (WarningBar *wb = qobject_cast<WarningBar*>(item->widget()))
        {
            ui->verticalLayout->removeWidget (item->widget());
            delete wb; // delete it immediately because a modal dialog might pop up
        }
    }
}
/*************************/
void FPwin::newTabFromName (const QString& fileName, bool multiple)
{
    if (!fileName.isEmpty()
        /* although loadText() takes care of folders, we don't want to open
           (a symlink to) /dev/null and then, get a prompt dialog on closing */
        && QFileInfo (fileName).isFile())
    {
        loadText (fileName, false, false, multiple);
    }
}
/*************************/
void FPwin::newTabFromRecent()
{
    QAction *action = qobject_cast<QAction*>(QObject::sender());
    if (!action) return;
    loadText (action->data().toString(), false, false, false);
}
/*************************/
void FPwin::fileOpen()
{
    if (isLoading()) return;

    /* find a suitable directory */
    int index = ui->tabWidget->currentIndex();
    QString fname;
    if (index > -1)
        fname = qobject_cast< TabPage *>(ui->tabWidget->widget (index))
                ->textEdit()->getFileName();

    QString path;
    if (!fname.isEmpty())
    {
        if (QFile::exists (fname))
            path = fname;
        else
        {
            QDir dir = QFileInfo (fname).absoluteDir();
            if (!dir.exists())
                dir = QDir::home();
            path = dir.path();
        }
    }
    else
    {
        /* I like the last opened file to be remembered */
        fname = lastFile_;
        if (!fname.isEmpty())
        {
            QDir dir = QFileInfo (fname).absoluteDir();
            if (!dir.exists())
                dir = QDir::home();
            path = dir.path();
        }
        else
        {
            QDir dir = QDir::home();
            path = dir.path();
        }
    }

    if (hasAnotherDialog()) return;
    disableShortcuts (true);
    QString filter = tr ("All Files (*)");
    if (!fname.isEmpty()
        && QFileInfo (fname).fileName().contains ('.'))
    {
        /* if relevant, do filtering to make opening of similar files easier */
        filter = tr ("All Files (*);;.%1 Files (*.%1)").arg (fname.section ('.', -1, -1));
    }
    FileDialog dialog (this);
    dialog.setAcceptMode (QFileDialog::AcceptOpen);
    dialog.setWindowTitle (tr ("Open file..."));
    dialog.setFileMode (QFileDialog::ExistingFiles);
    dialog.setNameFilter (filter);
    /*dialog.setLabelText (QFileDialog::Accept, tr ("Open"));
    dialog.setLabelText (QFileDialog::Reject, tr ("Cancel"));*/
    if (QFileInfo (path).isDir())
        dialog.setDirectory (path);
    else
    {
        dialog.selectFile (path);
        dialog.autoScroll();
    }
    if (dialog.exec())
    {
        QStringList files = dialog.selectedFiles();
        bool multiple (files.count() > 1 || isLoading());
        foreach (fname, files)
            newTabFromName (fname, multiple);
    }
    disableShortcuts (false);
}
/*************************/
// Check if the file is already opened for editing somewhere else.
bool FPwin::alreadyOpen (TabPage *tabPage) const
{
    bool res = false;

    QString fileName = tabPage->textEdit()->getFileName();
    FPsingleton *singleton = static_cast<FPsingleton*>(qApp);
    for (int i = 0; i < singleton->Wins.count(); ++i)
    {
        FPwin *thisOne = singleton->Wins.at (i);
        for (int j = 0; j < thisOne->ui->tabWidget->count(); ++j)
        {
            TabPage *thisTabPage = qobject_cast< TabPage *>(thisOne->ui->tabWidget->widget (j));
            if (thisOne == this && thisTabPage == tabPage)
                continue;
            TextEdit *thisTextEdit = thisTabPage->textEdit();
            if (thisTextEdit->getFileName() == fileName && !thisTextEdit->isReadOnly())
            {
                res = true;
                break;
            }
        }
        if (res) break;
    }
    return res;
}
/*************************/
void FPwin::enforceEncoding (QAction*)
{
    /* here, we don't need to check if some files are loading
       because encoding has no keyboard shortcut or tool button */

    int index = ui->tabWidget->currentIndex();
    if (index == -1) return;

    TextEdit *textEdit = qobject_cast< TabPage *>(ui->tabWidget->widget (index))->textEdit();
    QString fname = textEdit->getFileName();
    if (!fname.isEmpty())
    {
        if (unSaved (index, false))
        { // back to the previous encoding
            encodingToCheck (textEdit->getEncoding());
            return;
        }
        loadText (fname, true, true);
    }
    else
    {
        /* just change the statusbar text; the doc
           might be saved later with the new encoding */
        textEdit->setEncoding (checkToEncoding());
        if (ui->statusBar->isVisible())
        {
            QLabel *statusLabel = ui->statusBar->findChild<QLabel *>();
            QString str = statusLabel->text();
            QString encodStr = tr ("Encoding");
            // the next info is about lines; there's no syntax info
            QString lineStr = "</i>&nbsp;&nbsp;&nbsp;&nbsp;<b>" + tr ("Lines");
            int i = str.indexOf (encodStr);
            int j = str.indexOf (lineStr);
            int offset = encodStr.count() + 9; // size of ":</b> <i>"
            str.replace (i + offset, j - i - offset, checkToEncoding());
            statusLabel->setText (str);
        }
    }
}
/*************************/
void FPwin::reload()
{
    if (isLoading()) return;

    int index = ui->tabWidget->currentIndex();
    if (index == -1) return;

    if (unSaved (index, false)) return;

    QString fname = qobject_cast< TabPage *>(ui->tabWidget->widget (index))
                    ->textEdit()->getFileName();
    if (!fname.isEmpty()) loadText (fname, false, true);
}
/*************************/
// This is for both "Save" and "Save As"
bool FPwin::saveFile (bool keepSyntax)
{
    if (!isReady()) return false;

    int index = ui->tabWidget->currentIndex();
    if (index == -1) return false;

    TextEdit *textEdit = qobject_cast< TabPage *>(ui->tabWidget->widget (index))->textEdit();
    QString fname = textEdit->getFileName();
    if (fname.isEmpty()) fname = lastFile_;
    QString filter = tr ("All Files (*)");
    if (!fname.isEmpty()
        && QFileInfo (fname).fileName().contains ('.'))
    {
        /* if relevant, do filtering to prevent disastrous overwritings */
        filter = tr (".%1 Files (*.%1);;All Files (*)").arg (fname.section ('.', -1, -1));
    }

    if (fname.isEmpty()
        || !QFile::exists (fname)
        || textEdit->getFileName().isEmpty())
    {
        bool restorable = false;
        if (fname.isEmpty())
        {
            QDir dir = QDir::home();
            fname = dir.filePath (tr ("Untitled"));
        }
        else if (!QFile::exists (fname))
        {
            QDir dir = QFileInfo (fname).absoluteDir();
            if (!dir.exists())
            {
                dir = QDir::home();
                if (textEdit->getFileName().isEmpty())
                    filter = tr ("All Files (*)");
            }
            /* if the removed file is opened in this tab and its
               containing folder still exists, it's restorable */
            else if (!textEdit->getFileName().isEmpty())
                restorable = true;

            /* add the file name */
            if (!textEdit->getFileName().isEmpty())
                fname = dir.filePath (QFileInfo (fname).fileName());
            else
                fname = dir.filePath (tr ("Untitled"));
        }
        else
            fname = QFileInfo (fname).absoluteDir().filePath (tr ("Untitled"));

        /* use Save-As for Save or saving */
        if (!restorable
            && QObject::sender() != ui->actionSaveAs
            && QObject::sender() != ui->actionSaveCodec)
        {
            if (hasAnotherDialog()) return false;
            disableShortcuts (true);
            FileDialog dialog (this);
            dialog.setAcceptMode (QFileDialog::AcceptSave);
            dialog.setWindowTitle (tr ("Save as..."));
            dialog.setFileMode (QFileDialog::AnyFile);
            dialog.setNameFilter (filter);
            dialog.selectFile (fname);
            dialog.autoScroll();
            /*dialog.setLabelText (QFileDialog::Accept, tr ("Save"));
            dialog.setLabelText (QFileDialog::Reject, tr ("Cancel"));*/
            if (dialog.exec())
            {
                fname = dialog.selectedFiles().at (0);
                if (fname.isEmpty() || QFileInfo (fname).isDir())
                {
                    disableShortcuts (false);
                    return false;
                }
            }
            else
            {
                disableShortcuts (false);
                return false;
            }
            disableShortcuts (false);
        }
    }

    if (QObject::sender() == ui->actionSaveAs)
    {
        if (hasAnotherDialog()) return false;
        disableShortcuts (true);
        FileDialog dialog (this);
        dialog.setAcceptMode (QFileDialog::AcceptSave);
        dialog.setWindowTitle (tr ("Save as..."));
        dialog.setFileMode (QFileDialog::AnyFile);
        dialog.setNameFilter (filter);
        dialog.selectFile (fname);
        dialog.autoScroll();
        /*dialog.setLabelText (QFileDialog::Accept, tr ("Save"));
        dialog.setLabelText (QFileDialog::Reject, tr ("Cancel"));*/
        if (dialog.exec())
        {
            fname = dialog.selectedFiles().at (0);
            if (fname.isEmpty() || QFileInfo (fname).isDir())
            {
                disableShortcuts (false);
                return false;
            }
        }
        else
        {
            disableShortcuts (false);
            return false;
        }
        disableShortcuts (false);
    }
    else if (QObject::sender() == ui->actionSaveCodec)
    {
        if (hasAnotherDialog()) return false;
        disableShortcuts (true);
        FileDialog dialog (this);
        dialog.setAcceptMode (QFileDialog::AcceptSave);
        dialog.setWindowTitle (tr ("Keep encoding and save as..."));
        dialog.setFileMode (QFileDialog::AnyFile);
        dialog.setNameFilter (filter);
        dialog.selectFile (fname);
        dialog.autoScroll();
        /*dialog.setLabelText (QFileDialog::Accept, tr ("Save"));
        dialog.setLabelText (QFileDialog::Reject, tr ("Cancel"));*/
        if (dialog.exec())
        {
            fname = dialog.selectedFiles().at (0);
            if (fname.isEmpty() || QFileInfo (fname).isDir())
            {
                disableShortcuts (false);
                return false;
            }
        }
        else
        {
            disableShortcuts (false);
            return false;
        }
        disableShortcuts (false);
    }

    Config& config = static_cast<FPsingleton*>(qApp)->getConfig();
    if (config.getAppendEmptyLine()
        && !textEdit->document()->lastBlock().text().isEmpty())
    {
        QTextCursor tmpCur = textEdit->textCursor();
        tmpCur.beginEditBlock();
        tmpCur.movePosition(QTextCursor::End);
        tmpCur.insertBlock();
        tmpCur.endEditBlock();
    }

    /* now, try to write */
    QTextDocumentWriter writer (fname, "plaintext");
    bool success = false;
    if (QObject::sender() == ui->actionSaveCodec)
    {
        QString encoding  = checkToEncoding();

        if (hasAnotherDialog()) return false;
        disableShortcuts (true);
        MessageBox msgBox (this);
        msgBox.setIcon (QMessageBox::Question);
        msgBox.addButton (QMessageBox::Yes);
        msgBox.addButton (QMessageBox::No);
        msgBox.addButton (QMessageBox::Cancel);
        msgBox.changeButtonText (QMessageBox::Yes, tr ("Yes"));
        msgBox.changeButtonText (QMessageBox::No, tr ("No"));
        msgBox.changeButtonText (QMessageBox::Cancel, tr ("Cancel"));
        msgBox.setText ("<center>" + tr ("Do you want to use <b>MS Windows</b> end-of-lines?") + "</center>");
        msgBox.setInformativeText ("<center><i>" + tr ("This may be good for readability under MS Windows.") + "</i></center>");
        msgBox.setWindowModality (Qt::WindowModal);
        QString contents;
        int ln;
        QTextCodec *codec;
        QByteArray encodedString;
        const char *txt;
        switch (msgBox.exec()) {
        case QMessageBox::Yes:
            contents = textEdit->document()->toPlainText();
            contents.replace ("\n", "\r\n");
            ln = contents.length(); // for fwrite();
            codec = QTextCodec::codecForName (checkToEncoding().toUtf8());
            encodedString = codec->fromUnicode (contents);
            txt = encodedString.constData();
            if (encoding != "UTF-16")
            {
                std::ofstream file;
                file.open (fname.toUtf8().constData());
                if (file.is_open())
                {
                    file << txt;
                    file.close();
                    success = true;
                }
            }
            else
            {
                FILE * file;
                file = fopen (fname.toUtf8().constData(), "wb");
                if (file != nullptr)
                {
                    /* this worked correctly as I far as I tested */
                    fwrite (txt , 2 , ln + 1 , file);
                    fclose (file);
                    success = true;
                }
            }
            break;
        case QMessageBox::No:
            writer.setCodec (QTextCodec::codecForName (encoding.toUtf8()));
            break;
        default:
            disableShortcuts (false);
            return false;
            break;
        }
        disableShortcuts (false);
    }
    if (!success)
        success = writer.write (textEdit->document());

    if (success)
    {
        QFileInfo fInfo (fname);

        textEdit->document()->setModified (false);
        textEdit->setFileName (fname);
        textEdit->setSize (fInfo.size());
        ui->actionReload->setDisabled (false);
        setTitle (fname);
        QString tip (fInfo.absolutePath() + "/");
        QFontMetrics metrics (QToolTip::font());
        int w = QApplication::desktop()->screenGeometry().width();
        if (w > 200 * metrics.width (' ')) w = 200 * metrics.width (' ');
        QString elidedTip = metrics.elidedText (tip, Qt::ElideMiddle, w);
        ui->tabWidget->setTabToolTip (index, elidedTip);
        lastFile_ = fname;
        config.addRecentFile (lastFile_);
        if (!keepSyntax)
        { // uninstall and reinstall the syntax highlgihter if the programming language is changed
            QString prevLan = textEdit->getProg();
            setProgLang (textEdit);
            if (prevLan != textEdit->getProg())
            {
                if (ui->statusBar->isVisible()
                    && textEdit->getWordNumber() != -1)
                { // we want to change the statusbar text below
                    disconnect (textEdit, &QPlainTextEdit::textChanged, this, &FPwin::wordButtonStatus);
                }

                if (Highlighter *highlighter = qobject_cast< Highlighter *>(textEdit->getHighlighter()))
                {
                    disconnect (textEdit, &QPlainTextEdit::cursorPositionChanged, this, &FPwin::matchBrackets);
                    disconnect (textEdit, &QPlainTextEdit::blockCountChanged, this, &FPwin::formatOnBlockChange);
                    disconnect (textEdit, &TextEdit::updateRect, this, &FPwin::formatVisibleText);
                    disconnect (textEdit, &TextEdit::resized, this, &FPwin::formatOnResizing);

                    /* remove bracket highlights */
                    QList<QTextEdit::ExtraSelection> es = textEdit->extraSelections();
                    int n = textEdit->getRedSel().count();
                    while (n > 0 && !es.isEmpty())
                    {
                        es.removeLast();
                        --n;
                    }
                    textEdit->setRedSel (QList<QTextEdit::ExtraSelection>());
                    textEdit->setExtraSelections (es);

                    textEdit->setHighlighter (nullptr);
                    delete highlighter; highlighter = nullptr;
                }
                if (ui->actionSyntax->isChecked()) // not needed really
                    syntaxHighlighting (textEdit);
                if (ui->statusBar->isVisible())
                { // correct the statusbar text just by replacing the old syntax info
                    QLabel *statusLabel = ui->statusBar->findChild<QLabel *>();
                    QString str = statusLabel->text();
                    QString syntaxStr = tr ("Syntax");
                    int i = str.indexOf (syntaxStr);
                    if (i == -1) // there was no language before saving (prevLan.isEmpty())
                    {
                        QString lineStr = "&nbsp;&nbsp;&nbsp;&nbsp;<b>" + tr ("Lines");
                        int j = str.indexOf (lineStr);
                        syntaxStr = "&nbsp;&nbsp;&nbsp;&nbsp;<b>" + tr ("Syntax") + QString (":</b> <i>%1</i>")
                                                                                    .arg (textEdit->getProg());
                        str.insert (j, syntaxStr);
                    }
                    else
                    {
                        if (textEdit->getProg().isEmpty()) // there's no language after saving
                        {
                            syntaxStr = "&nbsp;&nbsp;&nbsp;&nbsp;<b>" + tr ("Syntax");
                            QString lineStr = "&nbsp;&nbsp;&nbsp;&nbsp;<b>" + tr ("Lines");
                            int j = str.indexOf (syntaxStr);
                            int k = str.indexOf (lineStr);
                            str.remove (j, k - j);
                        }
                        else // the language is changed by saving
                        {
                            QString lineStr = "</i>&nbsp;&nbsp;&nbsp;&nbsp;<b>" + tr ("Lines");
                            int j = str.indexOf (lineStr);
                            int offset = syntaxStr.count() + 9; // size of ":</b> <i>"
                            str.replace (i + offset, j - i - offset, textEdit->getProg());
                        }
                    }
                    statusLabel->setText (str);
                    if (textEdit->getWordNumber() != -1)
                        connect (textEdit, &QPlainTextEdit::textChanged, this, &FPwin::wordButtonStatus);
                }
            }
        }
    }
    else
    {
        QString str = writer.device()->errorString();
        showWarningBar ("<center><b><big>" + tr ("Cannot be saved!") + "</big></b></center>\n"
                        + "<center><i>" + QString ("<center><i>%1.</i></center>").arg (str) + "<i/></center>");
    }

    if (success && textEdit->getProg() == "help")
         QTimer::singleShot (0, this, SLOT (makeEditable()));

    return success;
}
/*************************/
void FPwin::fileSave()
{
    saveFile (false);
}
/*************************/
void FPwin::cutText()
{
    int index = ui->tabWidget->currentIndex();
    if (index == -1) return;

    qobject_cast< TabPage *>(ui->tabWidget->widget (index))->textEdit()->cut();
}
/*************************/
void FPwin::copyText()
{
    int index = ui->tabWidget->currentIndex();
    if (index == -1) return;

    qobject_cast< TabPage *>(ui->tabWidget->widget (index))->textEdit()->copy();
}
/*************************/
void FPwin::pasteText()
{
    int index = ui->tabWidget->currentIndex();
    if (index == -1) return;

    qobject_cast< TabPage *>(ui->tabWidget->widget (index))->textEdit()->paste();
}
/*************************/
void FPwin::deleteText()
{
    int index = ui->tabWidget->currentIndex();
    if (index == -1) return;

    TextEdit *textEdit = qobject_cast< TabPage *>(ui->tabWidget->widget (index))->textEdit();
    if (!textEdit->isReadOnly())
        textEdit->insertPlainText ("");
}
/*************************/
void FPwin::selectAllText()
{
    qobject_cast< TabPage *>(ui->tabWidget->currentWidget())->textEdit()->selectAll();
}
/*************************/
void FPwin::makeEditable()
{
    if (!isReady()) return;

    int index = ui->tabWidget->currentIndex();
    if (index == -1) return;

    TextEdit *textEdit = qobject_cast< TabPage *>(ui->tabWidget->widget (index))->textEdit();
    bool textIsSelected = textEdit->textCursor().hasSelection();

    textEdit->setReadOnly (false);
    Config config = static_cast<FPsingleton*>(qApp)->getConfig();
    if (!textEdit->hasDarkScheme())
    {
        textEdit->viewport()->setStyleSheet (QString (".QWidget {"
                                                      "color: black;"
                                                      "background-color: rgb(%1, %1, %1);}")
                                             .arg (config.getLightBgColorValue()));
    }
    else
    {
        textEdit->viewport()->setStyleSheet (QString (".QWidget {"
                                                      "color: white;"
                                                      "background-color: rgb(%1, %1, %1);}")
                                             .arg (config.getDarkBgColorValue()));
    }
    ui->actionEdit->setVisible (false);

    ui->actionPaste->setEnabled (true);
    ui->actionCopy->setEnabled (textIsSelected);
    ui->actionCut->setEnabled (textIsSelected);
    ui->actionDelete->setEnabled (textIsSelected);
    connect (textEdit, &QPlainTextEdit::copyAvailable, ui->actionCut, &QAction::setEnabled);
    connect (textEdit, &QPlainTextEdit::copyAvailable, ui->actionDelete, &QAction::setEnabled);
}
/*************************/
void FPwin::undoing()
{
    int index = ui->tabWidget->currentIndex();
    if (index == -1) return;

    TextEdit *textEdit = qobject_cast< TabPage *>(ui->tabWidget->widget (index))->textEdit();

    /* remove green highlights */
    textEdit->setGreenSel (QList<QTextEdit::ExtraSelection>());
    if (textEdit->getSearchedText().isEmpty()) // hlight() won't be called
    {
        QList<QTextEdit::ExtraSelection> es;
        if (ui->actionLineNumbers->isChecked() || ui->spinBox->isVisible())
            es.prepend (textEdit->currentLineSelection());
        es.append (textEdit->getRedSel());
        textEdit->setExtraSelections (es);
    }

    textEdit->undo();
}
/*************************/
void FPwin::redoing()
{
    int index = ui->tabWidget->currentIndex();
    if (index == -1) return;

    qobject_cast< TabPage *>(ui->tabWidget->widget (index))->textEdit()->redo();
}
/*************************/
// Change the window title and the search entry when switching tabs and...
void FPwin::tabSwitch (int index)
{
    if (index == -1)
    {
        setWindowTitle ("FeatherPad[*]");
        setWindowModified (false);
        return;
    }

    closeWarningBar();

    TabPage *tabPage = qobject_cast< TabPage *>(ui->tabWidget->widget (index));
    TextEdit *textEdit = tabPage->textEdit();
    if (!tabPage->isSearchBarVisible())
        textEdit->setFocus();
    QString fname = textEdit->getFileName();
    bool modified (textEdit->document()->isModified());

    QString shownName;
    if (modified)
    {
        if (fname.isEmpty())
            shownName = tr ("*Untitled");
        else
            shownName = QFileInfo (fname).fileName().prepend("*");
    }
    else
    {
        if (fname.isEmpty())
        {
            if (textEdit->getProg() == "help")
                shownName = "** " + tr ("Help") + " **";
            else
                shownName = tr ("Untitled");
        }
        else
            shownName = QFileInfo (fname).fileName();
    }
    shownName.replace ("\n", " ");
    setWindowTitle (shownName);

    /* although the window size, wrapping state or replacing text may have changed or
       the replace dock may have been closed, hlight() will be called automatically */
    //if (!textEdit->getSearchedText().isEmpty()) hlight();

    /* correct the encoding menu */
    encodingToCheck (textEdit->getEncoding());

    /* correct the states of some buttons */
    ui->actionUndo->setEnabled (textEdit->document()->isUndoAvailable());
    ui->actionRedo->setEnabled (textEdit->document()->isRedoAvailable());
    ui->actionSave->setEnabled (modified);
    if (fname.isEmpty())
        ui->actionReload->setEnabled (false);
    else
        ui->actionReload->setEnabled (true);
    bool readOnly = textEdit->isReadOnly();
    if (fname.isEmpty()
        && !modified
        && !textEdit->document()->isEmpty()) // 'Help' is the exception
    {
        ui->actionEdit->setVisible (false);
    }
    else
    {
        ui->actionEdit->setVisible (readOnly);
    }
    ui->actionPaste->setEnabled (!readOnly);
    bool textIsSelected = textEdit->textCursor().hasSelection();
    ui->actionCopy->setEnabled (textIsSelected);
    ui->actionCut->setEnabled (!readOnly && textIsSelected);
    ui->actionDelete->setEnabled (!readOnly && textIsSelected);

    if (isScriptLang (textEdit->getProg()) && QFileInfo (fname).isExecutable())
    {
        Config& config = static_cast<FPsingleton*>(qApp)->getConfig();
        ui->actionRun->setVisible (config.getExecuteScripts());
    }
    else
        ui->actionRun->setVisible (false);

    /* handle the spinbox */
    if (ui->spinBox->isVisible())
        ui->spinBox->setMaximum (textEdit->document()->blockCount());

    /* handle the statusbar */
    if (ui->statusBar->isVisible())
    {
        statusMsgWithLineCount (textEdit->document()->blockCount());
        QToolButton *wordButton = ui->statusBar->findChild<QToolButton *>();
        if (textEdit->getWordNumber() == -1)
        {
            if (wordButton)
                wordButton->setVisible (true);
            if (textEdit->document()->isEmpty()) // make an exception
                wordButtonStatus();
        }
        else
        {
            if (wordButton)
                wordButton->setVisible (false);
            QLabel *statusLabel = ui->statusBar->findChild<QLabel *>();
            statusLabel->setText (QString ("%1 <i>%2</i>")
                                  .arg (statusLabel->text())
                                  .arg (textEdit->getWordNumber()));
        }
    }

    /* al last, set the title of Replacment dock */
    if (ui->dockReplace->isVisible())
    {
        QString title = textEdit->getReplaceTitle();
        if (!title.isEmpty())
            ui->dockReplace->setWindowTitle (title);
        else
            ui->dockReplace->setWindowTitle (tr ("Rep&lacement"));
    }
    else
        textEdit->setReplaceTitle (QString());
}
/*************************/
void FPwin::fontDialog()
{
    if (isLoading()) return;

    int index = ui->tabWidget->currentIndex();
    if (index == -1) return;

    if (hasAnotherDialog()) return;
    disableShortcuts (true);

    TextEdit *textEdit = qobject_cast< TabPage *>(ui->tabWidget->widget (index))->textEdit();

    QFont currentFont = textEdit->document()->defaultFont();
    QFontDialog fd (currentFont, this);
    fd.setOption (QFontDialog::DontUseNativeDialog);
    fd.setWindowModality (Qt::WindowModal);
    fd.move (this->x() + this->width()/2 - fd.width()/2,
             this->y() + this->height()/2 - fd.height()/ 2);
    if (fd.exec())
    {
        QFont newFont = fd.selectedFont();
        FPsingleton *singleton = static_cast<FPsingleton*>(qApp);
        for (int i = 0; i < singleton->Wins.count(); ++i)
        {
            FPwin *thisWin = singleton->Wins.at (i);
            for (int j = 0; j < thisWin->ui->tabWidget->count(); ++j)
            {
                TextEdit *thisTextEdit = qobject_cast< TabPage *>(thisWin->ui->tabWidget->widget (j))->textEdit();
                thisTextEdit->document()->setDefaultFont (newFont);
                QFontMetrics metrics (newFont);
                thisTextEdit->setTabStopWidth (4 * metrics.width (' '));
            }
        }

        Config& config = static_cast<FPsingleton*>(qApp)->getConfig();
        if (config.getRemFont())
            config.setFont (newFont);

        /* the font can become larger... */
        QTimer::singleShot (0, textEdit, SLOT (updateEditorGeometry()));
        /* ... or smaller */
        reformat (textEdit);
    }
    disableShortcuts (false);
}
/*************************/
void FPwin::changeEvent (QEvent *event)
{
    Config& config = static_cast<FPsingleton*>(qApp)->getConfig();
    if (config.getRemSize() && event->type() == QEvent::WindowStateChange)
    {
        if (windowState() == Qt::WindowFullScreen)
        {
            config.setIsFull (true);
            config.setIsMaxed (false);
        }
        else if (windowState() == (Qt::WindowFullScreen ^ Qt::WindowMaximized))
        {
            config.setIsFull (true);
            config.setIsMaxed (true);
        }
        else
        {
            config.setIsFull (false);
            if (windowState() == Qt::WindowMaximized)
                config.setIsMaxed (true);
            else
                config.setIsMaxed (false);
        }
    }
    QWidget::changeEvent (event);
}
/*************************/
void FPwin::showHideSearch()
{
    if (!isReady()) return;

    int index = ui->tabWidget->currentIndex();
    if (index == -1) return;

    TabPage *tabPage = qobject_cast< TabPage *>(ui->tabWidget->widget (index));
    bool isFocused = tabPage->isSearchBarVisible() && tabPage->searchBarHasFocus();

    if (!isFocused)
        tabPage->focusSearchBar();
    else
    {
        ui->dockReplace->setVisible (false); // searchbar is needed by replace dock
        /* return focus to the document,... */
        tabPage->textEdit()->setFocus();
    }

    int count = ui->tabWidget->count();
    for (int indx = 0; indx < count; ++indx)
    {
        TabPage *page = qobject_cast< TabPage *>(ui->tabWidget->widget (indx));
        if (isFocused)
        {
            /* ... remove all yellow and green highlights... */
            TextEdit *textEdit = page->textEdit();
            textEdit->setSearchedText (QString());
            QList<QTextEdit::ExtraSelection> es;
            textEdit->setGreenSel (es); // not needed
            if (ui->actionLineNumbers->isChecked() || ui->spinBox->isVisible())
                es.prepend (textEdit->currentLineSelection());
            es.append (textEdit->getRedSel());
            textEdit->setExtraSelections (es);
            /* ... and empty all search entries */
            page->clearSearchEntry();
        }
        page->setSearchBarVisible (!isFocused);
    }
}
/*************************/
void FPwin::jumpTo()
{
    if (!isReady()) return;

    bool visibility = ui->spinBox->isVisible();

    for (int i = 0; i < ui->tabWidget->count(); ++i)
    {
        TextEdit *thisTextEdit = qobject_cast< TabPage *>(ui->tabWidget->widget (i))->textEdit();
        if (!ui->actionLineNumbers->isChecked())
            thisTextEdit->showLineNumbers (!visibility);

        if (!visibility)
        {
            /* setMaximum() isn't a slot */
            connect (thisTextEdit->document(),
                     &QTextDocument::blockCountChanged,
                     this,
                     &FPwin::setMax);
        }
        else
            disconnect (thisTextEdit->document(),
                        &QTextDocument::blockCountChanged,
                        this,
                        &FPwin::setMax);
    }

    TabPage *tabPage = qobject_cast< TabPage *>(ui->tabWidget->currentWidget());
    if (tabPage)
    {
        if (!visibility && ui->tabWidget->count() > 0)
            ui->spinBox->setMaximum (tabPage->textEdit()
                                     ->document()
                                     ->blockCount());
    }
    ui->spinBox->setVisible (!visibility);
    ui->label->setVisible (!visibility);
    ui->checkBox->setVisible (!visibility);
    if (!visibility)
    {
        ui->spinBox->setFocus();
        ui->spinBox->selectAll();
    }
    else if (tabPage)/* return focus to doc */
        tabPage->textEdit()->setFocus();
}
/*************************/
void FPwin::setMax (const int max)
{
    ui->spinBox->setMaximum (max);
}
/*************************/
void FPwin::goTo()
{
    /* workaround for not being able to use returnPressed()
       because of protectedness of spinbox's QLineEdit */
    if (!ui->spinBox->hasFocus()) return;

    if (TabPage *tabPage = qobject_cast< TabPage *>(ui->tabWidget->currentWidget()))
    {
        TextEdit *textEdit = tabPage->textEdit();
        QTextBlock block = textEdit->document()->findBlockByNumber (ui->spinBox->value() - 1);
        int pos = block.position();
        QTextCursor start = textEdit->textCursor();
        if (ui->checkBox->isChecked())
            start.setPosition (pos, QTextCursor::KeepAnchor);
        else
            start.setPosition (pos);
        textEdit->setTextCursor (start);
    }
}
/*************************/
void FPwin::showLN (bool checked)
{
    int count = ui->tabWidget->count();
    if (count == 0) return;

    if (checked)
    {
        for (int i = 0; i < count; ++i)
            qobject_cast< TabPage *>(ui->tabWidget->widget (i))->textEdit()->showLineNumbers (true);
    }
    else if (!ui->spinBox->isVisible()) // also the spinBox affects line numbers visibility
    {
        for (int i = 0; i < count; ++i)
            qobject_cast< TabPage *>(ui->tabWidget->widget (i))->textEdit()->showLineNumbers (false);
    }
}
/*************************/
void FPwin::toggleWrapping()
{
    int count = ui->tabWidget->count();
    if (count == 0) return;

    if (ui->actionWrap->isChecked())
    {
        for (int i = 0; i < count; ++i)
            qobject_cast< TabPage *>(ui->tabWidget->widget (i))->textEdit()->setLineWrapMode (QPlainTextEdit::WidgetWidth);
    }
    else
    {
        for (int i = 0; i < count; ++i)
            qobject_cast< TabPage *>(ui->tabWidget->widget (i))->textEdit()->setLineWrapMode (QPlainTextEdit::NoWrap);
    }
}
/*************************/
void FPwin::toggleIndent()
{
    int count = ui->tabWidget->count();
    if (count == 0) return;

    if (ui->actionIndent->isChecked())
    {
        for (int i = 0; i < count; ++i)
            qobject_cast< TabPage *>(ui->tabWidget->widget (i))->textEdit()->setAutoIndentation (true);
    }
    else
    {
        for (int i = 0; i < count; ++i)
            qobject_cast< TabPage *>(ui->tabWidget->widget (i))->textEdit()->setAutoIndentation (false);
    }
}
/*************************/
void FPwin::encodingToCheck (const QString& encoding)
{
    if (encoding != "UTF-8")
        ui->actionOther->setDisabled (true);

    if (encoding == "UTF-8")
        ui->actionUTF_8->setChecked (true);
    else if (encoding == "UTF-16")
        ui->actionUTF_16->setChecked (true);
    else if (encoding == "CP1256")
        ui->actionWindows_Arabic->setChecked (true);
    else if (encoding == "ISO-8859-1")
        ui->actionISO_8859_1->setChecked (true);
    else if (encoding == "ISO-8859-15")
        ui->actionISO_8859_15->setChecked (true);
    else if (encoding == "CP1252")
        ui->actionWindows_1252->setChecked (true);
    else if (encoding == "CP1251")
        ui->actionCryllic_CP1251->setChecked (true);
    else if (encoding == "KOI8-U")
        ui->actionCryllic_KOI8_U->setChecked (true);
    else if (encoding == "ISO-8859-5")
        ui->actionCryllic_ISO_8859_5->setChecked (true);
    else if (encoding == "BIG5")
        ui->actionChineese_BIG5->setChecked (true);
    else if (encoding == "B18030")
        ui->actionChinese_GB18030->setChecked (true);
    else if (encoding == "ISO-2022-JP")
        ui->actionJapanese_ISO_2022_JP->setChecked (true);
    else if (encoding == "ISO-2022-JP-2")
        ui->actionJapanese_ISO_2022_JP_2->setChecked (true);
    else if (encoding == "ISO-2022-KR")
        ui->actionJapanese_ISO_2022_KR->setChecked (true);
    else if (encoding == "CP932")
        ui->actionJapanese_CP932->setChecked (true);
    else if (encoding == "EUC-JP")
        ui->actionJapanese_EUC_JP->setChecked (true);
    else if (encoding == "CP949")
        ui->actionKorean_CP949->setChecked (true);
    else if (encoding == "CP1361")
        ui->actionKorean_CP1361->setChecked (true);
    else if (encoding == "EUC-KR")
        ui->actionKorean_EUC_KR->setChecked (true);
    else
    {
        ui->actionOther->setDisabled (false);
        ui->actionOther->setChecked (true);
    }
}
/*************************/
const QString FPwin::checkToEncoding() const
{
    QString encoding;

    if (ui->actionUTF_8->isChecked())
        encoding = "UTF-8";
    else if (ui->actionUTF_16->isChecked())
        encoding = "UTF-16";
    else if (ui->actionWindows_Arabic->isChecked())
        encoding = "CP1256";
    else if (ui->actionISO_8859_1->isChecked())
        encoding = "ISO-8859-1";
    else if (ui->actionISO_8859_15->isChecked())
        encoding = "ISO-8859-15";
    else if (ui->actionWindows_1252->isChecked())
        encoding = "CP1252";
    else if (ui->actionCryllic_CP1251->isChecked())
        encoding = "CP1251";
    else if (ui->actionCryllic_KOI8_U->isChecked())
        encoding = "KOI8-U";
    else if (ui->actionCryllic_ISO_8859_5->isChecked())
        encoding = "ISO-8859-5";
    else if (ui->actionChineese_BIG5->isChecked())
        encoding = "BIG5";
    else if (ui->actionChinese_GB18030->isChecked())
        encoding = "B18030";
    else if (ui->actionJapanese_ISO_2022_JP->isChecked())
        encoding = "ISO-2022-JP";
    else if (ui->actionJapanese_ISO_2022_JP_2->isChecked())
        encoding = "ISO-2022-JP-2";
    else if (ui->actionJapanese_ISO_2022_KR->isChecked())
        encoding = "ISO-2022-KR";
    else if (ui->actionJapanese_CP932->isChecked())
        encoding = "CP932";
    else if (ui->actionJapanese_EUC_JP->isChecked())
        encoding = "EUC-JP";
    else if (ui->actionKorean_CP949->isChecked())
        encoding = "CP949";
    else if (ui->actionKorean_CP1361->isChecked())
        encoding = "CP1361";
    else if (ui->actionKorean_EUC_KR->isChecked())
        encoding = "EUC-KR";
    else
        encoding = "UTF-8";

    return encoding;
}
/*************************/
void FPwin::docProp()
{
    if (ui->statusBar->isVisible())
    {
        for (int i = 0; i < ui->tabWidget->count(); ++i)
        {
            TextEdit *thisTextEdit = qobject_cast< TabPage *>(ui->tabWidget->widget (i))->textEdit();
            disconnect (thisTextEdit, &QPlainTextEdit::blockCountChanged, this, &FPwin::statusMsgWithLineCount);
            disconnect (thisTextEdit, &QPlainTextEdit::selectionChanged, this, &FPwin::statusMsg);
        }
        ui->statusBar->setVisible (false);
        return;
    }

    int index = ui->tabWidget->currentIndex();
    if (index == -1) return;

    statusMsgWithLineCount (qobject_cast< TabPage *>(ui->tabWidget->widget (index))
                            ->textEdit()->document()->blockCount());
    for (int i = 0; i < ui->tabWidget->count(); ++i)
    {
        TextEdit *thisTextEdit = qobject_cast< TabPage *>(ui->tabWidget->widget (i))->textEdit();
        connect (thisTextEdit, &QPlainTextEdit::blockCountChanged, this, &FPwin::statusMsgWithLineCount);
        connect (thisTextEdit, &QPlainTextEdit::selectionChanged, this, &FPwin::statusMsg);
    }

    ui->statusBar->setVisible (true);
    if (QToolButton *wordButton = ui->statusBar->findChild<QToolButton *>())
        wordButton->setVisible (true);
    wordButtonStatus();
}
/*************************/
// Set the status bar text according to the block count.
void FPwin::statusMsgWithLineCount (const int lines)
{
    TextEdit *textEdit = qobject_cast< TabPage *>(ui->tabWidget->currentWidget())->textEdit();
    /* ensure that the signal comes from the active tab if this is about a tab a signal */
    if (qobject_cast<TextEdit*>(QObject::sender()) && QObject::sender() != textEdit)
        return;

    QLabel *statusLabel = ui->statusBar->findChild<QLabel *>();

    /* the order: Encoding -> Syntax -> Lines -> Sel. Chars -> Words */
    QString encodStr = "<b>" + tr ("Encoding") + QString (":</b> <i>%1</i>").arg (textEdit->getEncoding());
    QString syntaxStr;
    if (!textEdit->getProg().isEmpty())
        syntaxStr = "&nbsp;&nbsp;&nbsp;&nbsp;<b>" + tr ("Syntax") + QString (":</b> <i>%1</i>").arg (textEdit->getProg());
    QString lineStr = "&nbsp;&nbsp;&nbsp;&nbsp;<b>" + tr ("Lines") + QString (":</b> <i>%1</i>").arg (lines);
    QString selStr = "&nbsp;&nbsp;&nbsp;&nbsp;<b>" + tr ("Sel. Chars")
                     + QString (":</b> <i>%1</i>").arg (textEdit->textCursor().selectedText().size());
    QString wordStr = "&nbsp;&nbsp;&nbsp;&nbsp;<b>" + tr ("Words") + ":</b>";

    statusLabel->setText (encodStr + syntaxStr + lineStr + selStr + wordStr);
}
/*************************/
// Change the status bar text when the selection changes.
void FPwin::statusMsg()
{
    QLabel *statusLabel = ui->statusBar->findChild<QLabel *>();
    int sel = qobject_cast< TabPage *>(ui->tabWidget->currentWidget())->textEdit()
              ->textCursor().selectedText().size();
    QString str = statusLabel->text();
    QString selStr = tr ("Sel. Chars");
    QString wordStr = "&nbsp;&nbsp;&nbsp;&nbsp;<b>" + tr ("Words");
    int i = str.indexOf (selStr) + selStr.count();
    int j = str.indexOf (wordStr);
    if (sel == 0)
    {
        QString prevSel = str.mid (i + 9, j - i - 13); // j - i - 13 --> j - (i + 9[":</b> <i>]") - 4["</i>"]
        if (prevSel.toInt() == 0) return;
    }
    QString charN;
    charN.setNum (sel);
    str.replace (i + 9, j - i - 13, charN);
    statusLabel->setText (str);
}
/*************************/
void FPwin::wordButtonStatus()
{
    QToolButton *wordButton = ui->statusBar->findChild<QToolButton *>();
    if (!wordButton) return;
    int index = ui->tabWidget->currentIndex();
    if (index == -1) return;
    TextEdit *textEdit = qobject_cast< TabPage *>(ui->tabWidget->widget (index))->textEdit();
    /* ensure that the signal comes from the active tab if this is about a tab a signal */
    if (qobject_cast<TextEdit*>(QObject::sender()) && QObject::sender() != textEdit)
        return;

    if (wordButton->isVisible())
    {
        QLabel *statusLabel = ui->statusBar->findChild<QLabel *>();
        int words = textEdit->getWordNumber();
        if (words == -1)
        {
            words = textEdit->toPlainText()
                    .split (QRegExp("(\\s|\\n|\\r)+"), QString::SkipEmptyParts)
                    .count();
            textEdit->setWordNumber (words);
        }

        wordButton->setVisible (false);
        statusLabel->setText (QString ("%1 <i>%2</i>")
                              .arg (statusLabel->text())
                              .arg (words));
        connect (textEdit, &QPlainTextEdit::textChanged, this, &FPwin::wordButtonStatus);
    }
    else
    {
        disconnect (textEdit, &QPlainTextEdit::textChanged, this, &FPwin::wordButtonStatus);
        textEdit->setWordNumber (-1);
        wordButton->setVisible (true);
        statusMsgWithLineCount (textEdit->document()->blockCount());
    }
}
/*************************/
void FPwin::filePrint()
{
    if (isLoading()) return;

    int index = ui->tabWidget->currentIndex();
    if (index == -1) return;

    if (hasAnotherDialog()) return;
    disableShortcuts (true);

    TextEdit *textEdit = qobject_cast< TabPage *>(ui->tabWidget->widget (index))->textEdit();
    QPrinter printer (QPrinter::HighResolution);

    /* choose an appropriate name and directory */
    QString fileName = textEdit->getFileName();
    if (fileName.isEmpty())
    {
        QDir dir = QDir::home();
        fileName= dir.filePath (tr ("Untitled"));
    }
    if (printer.outputFormat() == QPrinter::PdfFormat)
        printer.setOutputFileName (fileName.append (".pdf"));
    /*else if (printer.outputFormat() == QPrinter::PostScriptFormat)
        printer.setOutputFileName (fileName.append (".ps"));*/

    QPrintDialog dlg (&printer, this);
    dlg.setWindowModality (Qt::WindowModal);
    if (textEdit->textCursor().hasSelection())
        dlg.setOption (QAbstractPrintDialog::PrintSelection);
    dlg.setWindowTitle (tr ("Print Document"));
    if (dlg.exec() == QDialog::Accepted)
        textEdit->print (&printer);

    disableShortcuts (false);
}
/*************************/
void FPwin::nextTab()
{
    if (isLoading()) return;

    int index = ui->tabWidget->currentIndex();
    if (index == -1) return;

    if (QWidget *widget = ui->tabWidget->widget (index + 1))
        ui->tabWidget->setCurrentWidget (widget);
    else if (static_cast<FPsingleton*>(qApp)->getConfig().getTabWrapAround())
        ui->tabWidget->setCurrentIndex (0);
}
/*************************/
void FPwin::previousTab()
{
    if (isLoading()) return;

    int index = ui->tabWidget->currentIndex();
    if (index == -1) return;

    if (QWidget *widget = ui->tabWidget->widget (index - 1))
        ui->tabWidget->setCurrentWidget (widget);
    else if (static_cast<FPsingleton*>(qApp)->getConfig().getTabWrapAround())
    {
        int count = ui->tabWidget->count();
        if (count > 0)
            ui->tabWidget->setCurrentIndex (count - 1);
    }
}
/*************************/
void FPwin::lastTab()
{
    if (isLoading()) return;

    int count = ui->tabWidget->count();
    if (count > 0)
        ui->tabWidget->setCurrentIndex (count - 1);
}
/*************************/
void FPwin::firstTab()
{
    if (isLoading()) return;

    if ( ui->tabWidget->count() > 0)
        ui->tabWidget->setCurrentIndex (0);
}
/*************************/
void FPwin::detachTab()
{
    if (!isReady()) return;

    int index = ui->tabWidget->currentIndex();
    if (index == -1 || ui->tabWidget->count() == 1)
    {
        ui->tabWidget->tabBar()->finishMouseMoveEvent();
        return;
    }

    /*****************************************************
     *****          Get all necessary info.          *****
     ***** Then, remove the tab but keep its widget. *****
     *****************************************************/

    QString tooltip = ui->tabWidget->tabToolTip (index);
    QString tabText = ui->tabWidget->tabText (index);
    QString title = windowTitle();
    bool hl = true;
    bool spin = false;
    bool ln = false;
    bool status = false;
    if (!ui->actionSyntax->isChecked())
        hl = false;
    if (ui->spinBox->isVisible())
        spin = true;
    if (ui->actionLineNumbers->isChecked())
        ln = true;
    if (ui->statusBar->isVisible())
        status = true;

    TabPage *tabPage = qobject_cast< TabPage *>(ui->tabWidget->widget (index));
    TextEdit *textEdit = tabPage->textEdit();

    disconnect (textEdit, &TextEdit::updateRect, this ,&FPwin::hlighting);
    disconnect (textEdit, &QPlainTextEdit::textChanged, this ,&FPwin::hlight);
    disconnect (textEdit, &QPlainTextEdit::textChanged, this, &FPwin::wordButtonStatus);
    disconnect (textEdit, &QPlainTextEdit::blockCountChanged, this, &FPwin::statusMsgWithLineCount);
    disconnect (textEdit, &QPlainTextEdit::selectionChanged, this, &FPwin::statusMsg);
    disconnect (textEdit, &QPlainTextEdit::copyAvailable, ui->actionCut, &QAction::setEnabled);
    disconnect (textEdit, &QPlainTextEdit::copyAvailable, ui->actionDelete, &QAction::setEnabled);
    disconnect (textEdit, &QPlainTextEdit::copyAvailable, ui->actionCopy, &QAction::setEnabled);
    disconnect (textEdit, &TextEdit::zoomedOut, this, &FPwin::reformat);
    disconnect (textEdit, &TextEdit::fileDropped, this, &FPwin::newTabFromName);
    disconnect (textEdit, &QPlainTextEdit::cursorPositionChanged, this, &FPwin::matchBrackets);
    disconnect (textEdit, &QPlainTextEdit::blockCountChanged, this, &FPwin::formatOnBlockChange);
    disconnect (textEdit, &TextEdit::updateRect, this, &FPwin::formatVisibleText);
    disconnect (textEdit, &TextEdit::resized, this, &FPwin::formatOnResizing);

    disconnect (textEdit->document(), &QTextDocument::blockCountChanged, this, &FPwin::setMax);
    disconnect (textEdit->document(), &QTextDocument::modificationChanged, this, &FPwin::asterisk);
    disconnect (textEdit->document(), &QTextDocument::undoAvailable, ui->actionUndo, &QAction::setEnabled);
    disconnect (textEdit->document(), &QTextDocument::redoAvailable, ui->actionRedo, &QAction::setEnabled);
    disconnect (textEdit->document(), &QTextDocument::modificationChanged, ui->actionSave, &QAction::setEnabled);

    disconnect (tabPage, &TabPage::find, this, &FPwin::find);
    disconnect (tabPage, &TabPage::searchFlagChanged, this, &FPwin::searchFlagChanged);

    /* for tabbar to be updated peoperly with tab reordering during a
       fast drag-and-drop, mouse should be released before tab removal */
    ui->tabWidget->tabBar()->releaseMouse();

    ui->tabWidget->removeTab (index);
    if (ui->tabWidget->count() == 1)
    {
        ui->actionDetachTab->setDisabled (true);
        ui->actionRightTab->setDisabled (true);
        ui->actionLeftTab->setDisabled (true);
        ui->actionLastTab->setDisabled (true);
        ui->actionFirstTab->setDisabled (true);
    }

    /*******************************************************************
     ***** create a new window and replace its tab by this widget. *****
     *******************************************************************/

    FPsingleton *singleton = static_cast<FPsingleton*>(qApp);
    FPwin * dropTarget = singleton->newWin ("");
    dropTarget->closeTabAtIndex (0);

    /* first, set the new info... */
    dropTarget->lastFile_ = textEdit->getFileName();
    textEdit->setGreenSel (QList<QTextEdit::ExtraSelection>());
    textEdit->setRedSel (QList<QTextEdit::ExtraSelection>());
    /* ... then insert the detached widget... */
    dropTarget->ui->tabWidget->insertTab (0, tabPage, tabText);
    /* ... and remove all yellow and green highlights
       (the yellow ones will be recreated later if needed) */
    QList<QTextEdit::ExtraSelection> es;
    if (ln || spin)
        es.prepend (textEdit->currentLineSelection());
    textEdit->setExtraSelections (es);

    /* at last, set all properties correctly */
    dropTarget->enableWidgets (true);
    dropTarget->setWindowTitle (title);
    dropTarget->ui->tabWidget->setTabToolTip (0, tooltip);
    /* reload buttons, syntax highlighting, jump bar, line numbers */
    dropTarget->encodingToCheck (textEdit->getEncoding());
    if (!textEdit->getFileName().isEmpty())
        dropTarget->ui->actionReload->setEnabled (true);
    if (!hl)
        dropTarget->ui->actionSyntax->setChecked (false);
    if (spin)
    {
        dropTarget->ui->spinBox->setVisible (true);
        dropTarget->ui->label->setVisible (true);
        dropTarget->ui->spinBox->setMaximum (textEdit->document()->blockCount());
        connect (textEdit->document(), &QTextDocument::blockCountChanged, dropTarget, &FPwin::setMax);
    }
    if (ln)
        dropTarget->ui->actionLineNumbers->setChecked (true);
    /* searching */
    if (!textEdit->getSearchedText().isEmpty())
    {
        connect (textEdit, &QPlainTextEdit::textChanged, dropTarget, &FPwin::hlight);
        connect (textEdit, &TextEdit::updateRect, dropTarget, &FPwin::hlighting);
        /* restore yellow highlights, which will automatically
           set the current line highlight if needed because the
           spin button and line number menuitem are set above */
        dropTarget->hlight();
    }
    /* status bar */
    if (status)
    {
        dropTarget->ui->statusBar->setVisible (true);
        dropTarget->statusMsgWithLineCount (textEdit->document()->blockCount());
        if (textEdit->getWordNumber() == -1)
        {
            if (QToolButton *wordButton = dropTarget->ui->statusBar->findChild<QToolButton *>())
                wordButton->setVisible (true);
        }
        else
        {
            if (QToolButton *wordButton = dropTarget->ui->statusBar->findChild<QToolButton *>())
                wordButton->setVisible (false);
            QLabel *statusLabel = dropTarget->ui->statusBar->findChild<QLabel *>();
            statusLabel->setText (QString ("%1 <i>%2</i>")
                                  .arg (statusLabel->text())
                                  .arg (textEdit->getWordNumber()));
            connect (textEdit, &QPlainTextEdit::textChanged, dropTarget, &FPwin::wordButtonStatus);
        }
        connect (textEdit, &QPlainTextEdit::blockCountChanged, dropTarget, &FPwin::statusMsgWithLineCount);
        connect (textEdit, &QPlainTextEdit::selectionChanged, dropTarget, &FPwin::statusMsg);
    }
    if (textEdit->lineWrapMode() == QPlainTextEdit::NoWrap)
        dropTarget->ui->actionWrap->setChecked (false);
    /* auto indentation */
    if (textEdit->getAutoIndentation() == false)
        dropTarget->ui->actionIndent->setChecked (false);
    /* the remaining signals */
    connect (textEdit->document(), &QTextDocument::undoAvailable, dropTarget->ui->actionUndo, &QAction::setEnabled);
    connect (textEdit->document(), &QTextDocument::redoAvailable, dropTarget->ui->actionRedo, &QAction::setEnabled);
    connect (textEdit->document(), &QTextDocument::modificationChanged, dropTarget->ui->actionSave, &QAction::setEnabled);
    connect (textEdit->document(), &QTextDocument::modificationChanged, dropTarget, &FPwin::asterisk);
    connect (textEdit, &QPlainTextEdit::copyAvailable, dropTarget->ui->actionCopy, &QAction::setEnabled);

    connect (tabPage, &TabPage::find, dropTarget, &FPwin::find);
    connect (tabPage, &TabPage::searchFlagChanged, dropTarget, &FPwin::searchFlagChanged);

    if (!textEdit->isReadOnly())
    {
        connect (textEdit, &QPlainTextEdit::copyAvailable, dropTarget->ui->actionCut, &QAction::setEnabled);
        connect (textEdit, &QPlainTextEdit::copyAvailable, dropTarget->ui->actionDelete, &QAction::setEnabled);
    }
    connect (textEdit, &TextEdit::fileDropped, dropTarget, &FPwin::newTabFromName);
    connect (textEdit, &TextEdit::zoomedOut, dropTarget, &FPwin::reformat);

    if (textEdit->getHighlighter())
    {
        dropTarget->matchBrackets();
        connect (textEdit, &QPlainTextEdit::cursorPositionChanged, dropTarget, &FPwin::matchBrackets);
        connect (textEdit, &QPlainTextEdit::blockCountChanged, dropTarget, &FPwin::formatOnBlockChange);
        connect (textEdit, &TextEdit::updateRect, dropTarget, &FPwin::formatVisibleText);
        connect (textEdit, &TextEdit::resized, dropTarget, &FPwin::formatOnResizing);
    }

    textEdit->setFocus();

    dropTarget->activateWindow();
    dropTarget->raise();
}
/*************************/
void FPwin::dropTab (QString str)
{
    QStringList list = str.split ("+", QString::SkipEmptyParts);
    if (list.count() != 2)
    {
        ui->tabWidget->tabBar()->finishMouseMoveEvent();
        return;
    }
    int index = list.at (1).toInt();
    if (index <= -1) // impossible
    {
        ui->tabWidget->tabBar()->finishMouseMoveEvent();
        return;
    }

    FPsingleton *singleton = static_cast<FPsingleton*>(qApp);
    FPwin *dragSource = nullptr;
    for (int i = 0; i < singleton->Wins.count(); ++i)
    {
        if (singleton->Wins.at (i)->winId() == (uint) list.at (0).toInt())
        {
            dragSource = singleton->Wins.at (i);
            break;
        }
    }
    if (dragSource == this
        || dragSource == nullptr) // impossible
    {
        ui->tabWidget->tabBar()->finishMouseMoveEvent();
        return;
    }

    closeWarningBar();
    dragSource->closeWarningBar();

    QString tooltip = dragSource->ui->tabWidget->tabToolTip (index);
    QString tabText = dragSource->ui->tabWidget->tabText (index);
    bool spin = false;
    bool ln = false;
    if (dragSource->ui->spinBox->isVisible())
        spin = true;
    if (dragSource->ui->actionLineNumbers->isChecked())
        ln = true;

    TabPage *tabPage = qobject_cast< TabPage *>(dragSource->ui->tabWidget->widget (index));
    TextEdit *textEdit = tabPage->textEdit();

    disconnect (textEdit, &TextEdit::updateRect, dragSource ,&FPwin::hlighting);
    disconnect (textEdit, &QPlainTextEdit::textChanged, dragSource ,&FPwin::hlight);
    disconnect (textEdit, &QPlainTextEdit::textChanged, dragSource, &FPwin::wordButtonStatus);
    disconnect (textEdit, &QPlainTextEdit::blockCountChanged, dragSource, &FPwin::statusMsgWithLineCount);
    disconnect (textEdit, &QPlainTextEdit::selectionChanged, dragSource, &FPwin::statusMsg);
    disconnect (textEdit, &QPlainTextEdit::copyAvailable, dragSource->ui->actionCut, &QAction::setEnabled);
    disconnect (textEdit, &QPlainTextEdit::copyAvailable, dragSource->ui->actionDelete, &QAction::setEnabled);
    disconnect (textEdit, &QPlainTextEdit::copyAvailable, dragSource->ui->actionCopy, &QAction::setEnabled);
    disconnect (textEdit, &TextEdit::zoomedOut, dragSource, &FPwin::reformat);
    disconnect (textEdit, &TextEdit::fileDropped, dragSource, &FPwin::newTabFromName);
    disconnect (textEdit, &QPlainTextEdit::cursorPositionChanged, dragSource, &FPwin::matchBrackets);
    disconnect (textEdit, &QPlainTextEdit::blockCountChanged, dragSource, &FPwin::formatOnBlockChange);
    disconnect (textEdit, &TextEdit::updateRect, dragSource, &FPwin::formatVisibleText);
    disconnect (textEdit, &TextEdit::resized, dragSource, &FPwin::formatOnResizing);

    disconnect (textEdit->document(), &QTextDocument::blockCountChanged, dragSource, &FPwin::setMax);
    disconnect (textEdit->document(), &QTextDocument::modificationChanged, dragSource, &FPwin::asterisk);
    disconnect (textEdit->document(), &QTextDocument::undoAvailable, dragSource->ui->actionUndo, &QAction::setEnabled);
    disconnect (textEdit->document(), &QTextDocument::redoAvailable, dragSource->ui->actionRedo, &QAction::setEnabled);
    disconnect (textEdit->document(), &QTextDocument::modificationChanged, dragSource->ui->actionSave, &QAction::setEnabled);

    disconnect (tabPage, &TabPage::find, dragSource, &FPwin::find);
    disconnect (tabPage, &TabPage::searchFlagChanged, dragSource, &FPwin::searchFlagChanged);

    /* it's important to release mouse before tab removal because otherwise, the source
       tabbar might not be updated properly with tab reordering during a fast drag-and-drop */
    dragSource->ui->tabWidget->tabBar()->releaseMouse();

    dragSource->ui->tabWidget->removeTab (index);
    int count = dragSource->ui->tabWidget->count();
    if (count == 1)
    {
        dragSource->ui->actionDetachTab->setDisabled (true);
        dragSource->ui->actionRightTab->setDisabled (true);
        dragSource->ui->actionLeftTab->setDisabled (true);
        dragSource->ui->actionLastTab->setDisabled (true);
        dragSource->ui->actionFirstTab->setDisabled (true);
    }

    /***************************************************************************
     ***** The tab is dropped into this window; so insert it as a new tab. *****
     ***************************************************************************/

    int insertIndex = ui->tabWidget->currentIndex() + 1;

    /* first, set the new info... */
    lastFile_ = textEdit->getFileName();
    textEdit->setGreenSel (QList<QTextEdit::ExtraSelection>());
    textEdit->setRedSel (QList<QTextEdit::ExtraSelection>());
    /* ... then insert the detached widget,
       considering whether the searchbar should be shown... */
    if (!textEdit->getSearchedText().isEmpty())
    {
        if (insertIndex == 0 // the window has no tab yet
            || !qobject_cast< TabPage *>(ui->tabWidget->widget (insertIndex - 1))->isSearchBarVisible())
        {
            for (int i = 0; i < ui->tabWidget->count(); ++i)
                qobject_cast< TabPage *>(ui->tabWidget->widget (i))->setSearchBarVisible (true);
        }
    }
    else if (insertIndex > 0)
    {
        tabPage->setSearchBarVisible (qobject_cast< TabPage *>(ui->tabWidget->widget (insertIndex - 1))
                                      ->isSearchBarVisible());
    }
    ui->tabWidget->insertTab (insertIndex, tabPage, tabText);
    ui->tabWidget->setCurrentIndex (insertIndex);
    /* ... and remove all yellow and green highlights
       (the yellow ones will be recreated later if needed) */
    QList<QTextEdit::ExtraSelection> es;
    if ((ln || spin)
        && (ui->actionLineNumbers->isChecked() || ui->spinBox->isVisible()))
    {
        es.prepend (textEdit->currentLineSelection());
    }
    textEdit->setExtraSelections (es);

    /* at last, set all properties correctly */
    if (ui->tabWidget->count() == 1)
        enableWidgets (true);
    ui->tabWidget->setTabToolTip (insertIndex, tooltip);
    /* detach button */
    if (ui->tabWidget->count() == 2)
    {
        ui->actionDetachTab->setEnabled (true);
        ui->actionRightTab->setEnabled (true);
        ui->actionLeftTab->setEnabled (true);
        ui->actionLastTab->setEnabled (true);
        ui->actionFirstTab->setEnabled (true);
    }
    /* reload buttons, syntax highlighting, jump bar, line numbers */
    if (ui->actionSyntax->isChecked() && !textEdit->getHighlighter())
        syntaxHighlighting (textEdit);
    else if (!ui->actionSyntax->isChecked() && textEdit->getHighlighter())
    {
        Highlighter *highlighter = qobject_cast< Highlighter *>(textEdit->getHighlighter());
        textEdit->setHighlighter (nullptr);
        delete highlighter; highlighter = nullptr;
    }
    if (ui->spinBox->isVisible())
        connect (textEdit->document(), &QTextDocument::blockCountChanged, this, &FPwin::setMax);
    if (ui->actionLineNumbers->isChecked() || ui->spinBox->isVisible())
        textEdit->showLineNumbers (true);
    else
        textEdit->showLineNumbers (false);
    /* searching */
    if (!textEdit->getSearchedText().isEmpty())
    {
        connect (textEdit, &QPlainTextEdit::textChanged, this, &FPwin::hlight);
        connect (textEdit, &TextEdit::updateRect, this, &FPwin::hlighting);
        /* restore yellow highlights, which will automatically
           set the current line highlight if needed because the
           spin button and line number menuitem are set above */
        hlight();
    }
    /* status bar */
    if (ui->statusBar->isVisible())
    {
        connect (textEdit, &QPlainTextEdit::blockCountChanged, this, &FPwin::statusMsgWithLineCount);
        connect (textEdit, &QPlainTextEdit::selectionChanged, this, &FPwin::statusMsg);
        if (textEdit->getWordNumber() != -1)
            connect (textEdit, &QPlainTextEdit::textChanged, this, &FPwin::wordButtonStatus);
    }
    if (ui->actionWrap->isChecked() && textEdit->lineWrapMode() == QPlainTextEdit::NoWrap)
        textEdit->setLineWrapMode (QPlainTextEdit::WidgetWidth);
    else if (!ui->actionWrap->isChecked() && textEdit->lineWrapMode() == QPlainTextEdit::WidgetWidth)
        textEdit->setLineWrapMode (QPlainTextEdit::NoWrap);
    /* auto indentation */
    if (ui->actionIndent->isChecked() && textEdit->getAutoIndentation() == false)
        textEdit->setAutoIndentation (true);
    else if (!ui->actionIndent->isChecked() && textEdit->getAutoIndentation() == true)
        textEdit->setAutoIndentation (false);
    /* the remaining signals */
    connect (textEdit->document(), &QTextDocument::undoAvailable, ui->actionUndo, &QAction::setEnabled);
    connect (textEdit->document(), &QTextDocument::redoAvailable, ui->actionRedo, &QAction::setEnabled);
    connect (textEdit->document(), &QTextDocument::modificationChanged, ui->actionSave, &QAction::setEnabled);
    connect (textEdit->document(), &QTextDocument::modificationChanged, this, &FPwin::asterisk);
    connect (textEdit, &QPlainTextEdit::copyAvailable, ui->actionCopy, &QAction::setEnabled);

    connect (tabPage, &TabPage::find, this, &FPwin::find);
    connect (tabPage, &TabPage::searchFlagChanged, this, &FPwin::searchFlagChanged);

    if (!textEdit->isReadOnly())
    {
        connect (textEdit, &QPlainTextEdit::copyAvailable, ui->actionCut, &QAction::setEnabled);
        connect (textEdit, &QPlainTextEdit::copyAvailable, ui->actionDelete, &QAction::setEnabled);
    }
    connect (textEdit, &TextEdit::fileDropped, this, &FPwin::newTabFromName);
    connect (textEdit, &TextEdit::zoomedOut, this, &FPwin::reformat);

    if (textEdit->getHighlighter()) // it's set to NULL above when syntax highlighting is disabled
    {
        matchBrackets();
        connect (textEdit, &QPlainTextEdit::cursorPositionChanged, this, &FPwin::matchBrackets);
        connect (textEdit, &QPlainTextEdit::blockCountChanged, this, &FPwin::formatOnBlockChange);
        connect (textEdit, &TextEdit::updateRect, this, &FPwin::formatVisibleText);
        connect (textEdit, &TextEdit::resized, this, &FPwin::formatOnResizing);
    }

    textEdit->setFocus();

    activateWindow();
    raise();

    if (count == 0)
        QTimer::singleShot (0, dragSource, SLOT (close()));
}
/*************************/
void FPwin::tabContextMenu (const QPoint& p)
{
    int tabNum = ui->tabWidget->count();
    QTabBar *tbar = ui->tabWidget->tabBar();
    rightClicked_ = tbar->tabAt (p);
    if (rightClicked_ < 0) return;

    QString fname = qobject_cast< TabPage *>(ui->tabWidget->widget (rightClicked_))
                    ->textEdit()->getFileName();
    QMenu menu;
    bool showMenu = false;
    if (tabNum > 1)
    {
        showMenu = true;
        if (rightClicked_ < tabNum - 1)
            menu.addAction (ui->actionCloseRight);
        if (rightClicked_ > 0)
            menu.addAction (ui->actionCloseLeft);
        menu.addSeparator();
        if (rightClicked_ < tabNum - 1 && rightClicked_ > 0)
            menu.addAction (ui->actionCloseOther);
        menu.addAction (ui->actionCloseAll);
        if (!fname.isEmpty())
            menu.addSeparator();
    }
    if (!fname.isEmpty())
    {
        showMenu = true;
        menu.addAction (ui->actionCopyName);
        menu.addAction (ui->actionCopyPath);
    }
    if (showMenu) // we don't want an empty menu
        menu.exec (tbar->mapToGlobal (p));
}
/*************************/
void FPwin::prefDialog()
{
    if (isLoading()) return;

    if (hasAnotherDialog()) return;
    disableShortcuts (true);
    PrefDialog dlg (this);
    /*dlg.show();
    move (x() + width()/2 - dlg.width()/2,
          y() + height()/2 - dlg.height()/ 2);*/
    dlg.exec();
    disableShortcuts (false);
}
/*************************/
void FPwin::manageSessions()
{
    if (!isReady()) return;

    /* first see whether the Sessions dialog is already open... */
    FPsingleton *singleton = static_cast<FPsingleton*>(qApp);
    for (int i = 0; i < singleton->Wins.count(); ++i)
    {
        QList<QDialog*> dialogs  = singleton->Wins.at (i)->findChildren<QDialog*>();
        for (int j = 0; j < dialogs.count(); ++j)
        {
            if (dialogs.at (j)->objectName() ==  "sessionDialog")
            {
                dialogs.at (j)->raise();
                dialogs.at (j)->activateWindow();
                return;
            }
        }
    }
    /* ... and if not, create a non-modal Sessions dialog */
    SessionDialog *dlg = new SessionDialog (this);
    dlg->setAttribute (Qt::WA_DeleteOnClose);
    dlg->show();
    /*move (x() + width()/2 - dlg.width()/2,
          y() + height()/2 - dlg.height()/ 2);*/
    dlg->raise();
    dlg->activateWindow();
}
/*************************/
void FPwin::aboutDialog()
{
    if (isLoading()) return;

    if (hasAnotherDialog()) return;
    disableShortcuts (true);
    MessageBox msgBox (this);
    msgBox.setText (QString ("<center><b><big>%1 %2</big></b></center><br>").arg (qApp->applicationName()).arg (qApp->applicationVersion()));
    msgBox.setInformativeText ("<center> " + tr ("A lightweight, tabbed, plain-text editor") + " </center>\n<center> "
                               + tr ("based on Qt5") + " </center><br><center> "
                               + tr ("Author")+": <a href='mailto:tsujan2000@gmail.com?Subject=My%20Subject'>Pedram Pourang ("
                               + tr ("aka.") + " Tsu Jan)</a> </center><p></p>");
    msgBox.setStandardButtons (QMessageBox::Ok);
    msgBox.changeButtonText (QMessageBox::Ok, tr ("Ok"));
    msgBox.setWindowModality (Qt::WindowModal);
    Config config = static_cast<FPsingleton*>(qApp)->getConfig();
    QIcon FPIcon;
    if (config.getSysIcon())
    {
        FPIcon = QIcon::fromTheme ("featherpad");
        if (FPIcon.isNull())
            FPIcon = QIcon (":icons/featherpad.svg");
    }
    else
        FPIcon = QIcon (":icons/featherpad.svg");
    msgBox.setIconPixmap (FPIcon.pixmap (64, 64));
    msgBox.setWindowTitle (tr ("About FeatherPad"));
    msgBox.exec();
    disableShortcuts (false);
}
/*************************/
void FPwin::helpDoc()
{
    int index = ui->tabWidget->currentIndex();
    if (index == -1)
        newTab();
    else
    {
        for (int i = 0; i < ui->tabWidget->count(); ++i)
        {
            TabPage *thisTabPage = qobject_cast< TabPage *>(ui->tabWidget->widget (i));
            TextEdit *thisTextEdit = thisTabPage->textEdit();
            if (thisTextEdit->getFileName().isEmpty()
                && !thisTextEdit->document()->isModified()
                && !thisTextEdit->document()->isEmpty())
            {
                ui->tabWidget->setCurrentIndex (ui->tabWidget->indexOf (thisTabPage));
                return;
            }
        }
    }

    QFile helpFile (DATADIR "/featherpad/help");

    if (!helpFile.exists()) return;
    if (!helpFile.open (QFile::ReadOnly)) return;

    TextEdit *textEdit = qobject_cast< TabPage *>(ui->tabWidget->currentWidget())->textEdit();
    if (!textEdit->document()->isEmpty()
        || textEdit->document()->isModified())
    {
        newTab();
        textEdit = qobject_cast< TabPage *>(ui->tabWidget->currentWidget())->textEdit();
    }

    QByteArray data = helpFile.readAll();
    helpFile.close();
    QTextCodec *codec = QTextCodec::codecForName ("UTF-8");
    QString str = codec->toUnicode (data);
    textEdit->setPlainText (str);

    textEdit->setReadOnly (true);
    if (!textEdit->hasDarkScheme())
        textEdit->viewport()->setStyleSheet (".QWidget {"
                                             "color: black;"
                                             "background-color: rgb(225, 238, 255);}");
    else
        textEdit->viewport()->setStyleSheet (".QWidget {"
                                             "color: white;"
                                             "background-color: rgb(0, 60, 110);}");
    ui->actionCut->setDisabled (true);
    ui->actionPaste->setDisabled (true);
    ui->actionDelete->setDisabled (true);
    disconnect (textEdit, &QPlainTextEdit::copyAvailable, ui->actionCut, &QAction::setEnabled);
    disconnect (textEdit, &QPlainTextEdit::copyAvailable, ui->actionDelete, &QAction::setEnabled);

    index = ui->tabWidget->currentIndex();
    textEdit->setEncoding ("UTF-8");
    textEdit->setWordNumber (-1);
    textEdit->setProg ("help"); // just for marking
    if (ui->statusBar->isVisible())
    {
        statusMsgWithLineCount (textEdit->document()->blockCount());
        if (QToolButton *wordButton = ui->statusBar->findChild<QToolButton *>())
            wordButton->setVisible (true);
    }
    encodingToCheck ("UTF-8");
    QString title = "** " + tr ("Help") + " **";
    ui->tabWidget->setTabText (index, title);
    setWindowTitle (title + "[*]");
    setWindowModified (false);
    ui->tabWidget->setTabToolTip (index, title);
}

}
