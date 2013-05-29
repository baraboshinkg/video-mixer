#include <QtGui>

#include "qwaitcursor.h"
#include "settings.h"
#include "storagesettings.h"
#include "studiessettings.h"
#include "videosettings.h"

#ifdef WITH_DICOM
#include "dicomdevicesettings.h"
#include "dicomserversettings.h"
#include "dicommwlsettings.h"
#include "dicommppssettings.h"
#include "dicomstoragesettings.h"
#include "worklistcolumnsettings.h"
#include "worklistquerysettings.h"
#endif

Q_DECLARE_METATYPE(QMetaObject)
static int QMetaObjectMetaType = qRegisterMetaType<QMetaObject>();

Settings::Settings(QWidget *parent, Qt::WindowFlags flags)
    : QDialog(parent, flags)
{
    listWidget = new QListWidget;
    listWidget->setMovement(QListView::Static);
    listWidget->setMaximumWidth(200);
    listWidget->setSpacing(2);

    pagesWidget = new QStackedWidget;
    pagesWidget->setMinimumSize(520, 360);

    createPages();

    QHBoxLayout *layoutContent = new QHBoxLayout;
    layoutContent->addWidget(listWidget);
    layoutContent->addWidget(pagesWidget, 1);

    QHBoxLayout *layoutBtns = new QHBoxLayout;
    layoutBtns->addStretch(1);

    QPushButton *btnApply = new QPushButton(tr("Appl&y"));
    connect(btnApply, SIGNAL(clicked()), this, SLOT(onClickApply()));
    layoutBtns->addWidget(btnApply);
    btnCancel = new QPushButton(tr("Cancel"));
    connect(btnCancel, SIGNAL(clicked()), this, SLOT(reject()));
    layoutBtns->addWidget(btnCancel);
    QPushButton *btnOk = new QPushButton(tr("Ok"));
    connect(btnOk, SIGNAL(clicked()), this, SLOT(accept()));
    btnOk->setDefault(true);
    layoutBtns->addWidget(btnOk);

    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->addLayout(layoutContent);
    mainLayout->addSpacing(12);
    mainLayout->addLayout(layoutBtns);
    setLayout(mainLayout);

    setWindowTitle(tr("Settings"));
}

void Settings::createPage(const QString& title, const QMetaObject& page)
{
    auto btn= new QListWidgetItem(title, listWidget);
    btn->setData(Qt::UserRole, QVariant::fromValue(page));
}

void Settings::createPages()
{
    createPage(tr("Video source"), VideoSettings::staticMetaObject);
    createPage(tr("Storage"), StorageSettings::staticMetaObject);
    createPage(tr("Studies"), StudiesSettings::staticMetaObject);

#ifdef WITH_DICOM
    createPage(tr("DICOM device"), DicomDeviceSettings::staticMetaObject);
    createPage(tr("DICOM MPPS"), DicomMppsSettings::staticMetaObject);
    createPage(tr("DICOM MWL"), DicomMwlSettings::staticMetaObject);
    createPage(tr("DICOM servers"), DicomServerSettings::staticMetaObject);
    createPage(tr("DICOM storage"), DicomStorageSettings::staticMetaObject);
    createPage(tr("Worklist column display"), WorklistColumnSettings::staticMetaObject);
    createPage(tr("Worklist query settings"), WorklistQuerySettings::staticMetaObject);
#endif

    // Sort pages using current locale
    //
    listWidget->sortItems();

    connect(listWidget,
        SIGNAL(currentItemChanged(QListWidgetItem*,QListWidgetItem*)),
        this, SLOT(changePage(QListWidgetItem*,QListWidgetItem*)));
}

void Settings::changePage(QListWidgetItem *current, QListWidgetItem *previous)
{
    if (!current)
        current = previous;

    const QVariant& data = current->data(Qt::UserRole);
    if (data.type() == QVariant::UserType)
    {
        const QMetaObject& mobj = data.value<QMetaObject>();
        QWidget* page = static_cast<QWidget*>(mobj.newInstance());
        auto count = pagesWidget->count();
        current->setData(Qt::UserRole, count);
        pagesWidget->addWidget(page);
        pagesWidget->setCurrentIndex(count);
        connect(this, SIGNAL(save()), page, SLOT(save()));
    }
    else
    {
        pagesWidget->setCurrentIndex(current->data(Qt::UserRole).toInt());
    }
}

void Settings::onClickApply()
{
    QWaitCursor wait(this);

    // Save add pages data.
    //
    save();

    // After all pages has been saved, we can trigger another signal
    // to tell the application that all settings are ready to be read.
    //
    apply();

    // Replace "Cancel" with "Close" since we can not cancel anymore
    //
    btnCancel->setText(tr("Close"));
}

void Settings::accept()
{
    save();
    QDialog::accept();
}
