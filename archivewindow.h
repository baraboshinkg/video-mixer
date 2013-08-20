/*
 * Copyright (C) 2013 Irkutsk Diagnostic Center.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ARCHIVEWINDOW_H
#define ARCHIVEWINDOW_H

#include <QDialog>
#include <QDir>
#include <QListView>

#include <QGst/Message>
#include <QGst/Pipeline>

QT_BEGIN_NAMESPACE
class QListWidget;
class QListWidgetItem;
class QStackedWidget;
class QToolBar;
QT_END_NAMESPACE

class ArchiveWindow : public QWidget
{
    Q_OBJECT
    QToolBar*              barPath;
    QToolBar*              barMediaControls;
    QAction*               actionDelete;
#ifdef WITH_DICOM
    QAction*               actionStore;
#endif
    QAction*               actionPlay;
    QAction*               actionMode;
    QAction*               actionSeekBack;
    QAction*               actionSeekFwd;
    QListWidget*           listFiles;
    QStackedWidget*        pagesWidget;
    QWidget*               player;
    QGst::PipelinePtr      pipeline;
    QDir                   root;
    QDir                   curr;

    void updatePath();
    void updateList();
    void stopMedia();
    void playMediaFile(const QFileInfo &fi);
    void onBusMessage(const QGst::MessagePtr& message);
    void onStateChangedMessage(const QGst::StateChangedMessagePtr& message);
    void createSubDirMenu(QAction* parentAction);
    void switchViewMode(int mode);

public:
    explicit ArchiveWindow(QWidget *parent = 0);
    ~ArchiveWindow();
protected:
    virtual void closeEvent(QCloseEvent *);

signals:
    
public slots:
    void updateRoot();
    void setPath(const QString& path);
    void selectPath(QAction* action);
    void selectPath(bool);
    void onListItemDoubleClicked(QListWidgetItem* item);
    void onListRowChanged(int idx);
    void onListKey();
    void onSwitchModeClick();
    void onSwitchModeClick(QAction* action);
    void onShowFolderClick();
    void onDeleteClick();
#ifdef WITH_DICOM
    void onStoreClick();
#endif
    void onPrevClick();
    void onNextClick();
    void onSeekClick();
    void onPlayPauseClick();
    void preparePathPopupMenu();
};

#endif // ARCHIVEWINDOW_H
