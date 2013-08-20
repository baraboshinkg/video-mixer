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

#include "dicommppsmwlsettings.h"
#include "comboboxwithpopupsignal.h"
#include <QCheckBox>
#include <QFormLayout>
#include <QSettings>

DicomMppsMwlSettings::DicomMppsMwlSettings(QWidget *parent) :
    QWidget(parent)
{
    auto mainLayout = new QFormLayout;
    mainLayout->addRow(checkUseMwl = new QCheckBox(tr("MWL &Function")), cbMwlServer = new ComboBoxWithPopupSignal);
    connect(checkUseMwl, SIGNAL(toggled(bool)), this, SLOT(onUseToggle(bool)));

    QSettings settings;
    QString mwlServer = settings.value("mwl-server").toString();
    if (mwlServer.isEmpty())
    {
        cbMwlServer->setEnabled(false);
    }
    else
    {
        cbMwlServer->addItem(mwlServer);
        cbMwlServer->setCurrentIndex(0);
        checkUseMwl->setChecked(true);
    }

    connect(cbMwlServer, SIGNAL(beforePopup()), this, SLOT(onUpdateServers()));

    mainLayout->addRow(checkTransCyr = new QCheckBox(tr("&Translate latin letters back to cyrillic")));
    checkTransCyr->setChecked(settings.value("translate-cyrillic", true).toBool());

    auto spacer = new QWidget;
    spacer->setMinimumHeight(16);
    mainLayout->addWidget(spacer);

    mainLayout->addRow(checkUseMpps = new QCheckBox(tr("MPPS F&unction")), cbMppsServer = new ComboBoxWithPopupSignal);
    connect(checkUseMpps, SIGNAL(toggled(bool)), this, SLOT(onUseToggle(bool)));
    mainLayout->addRow(checkStartWithMpps = new QCheckBox(tr("&In progress is automatically sent when an examitaion has been started")));
    mainLayout->addRow(checkCompleteWithMpps = new QCheckBox(tr("&Completed is automatically sent when an examitaion has been ended")));

    QString mppsServer = settings.value("mpps-server").toString();
    if (mppsServer.isEmpty())
    {
        cbMppsServer->setEnabled(false);
    }
    else
    {
        cbMppsServer->addItem(mppsServer);
        cbMppsServer->setCurrentIndex(0);
        checkUseMpps->setChecked(true);
    }
    checkStartWithMpps->setChecked(settings.value("start-with-mpps", true).toBool());
    checkCompleteWithMpps->setChecked(settings.value("complete-with-mpps", true).toBool());

    connect(cbMppsServer, SIGNAL(beforePopup()), this, SLOT(onUpdateServers()));

    setLayout(mainLayout);
}

void DicomMppsMwlSettings::onUpdateServers()
{
    auto cb = static_cast<QComboBox*>(sender());
    QStringList serverNames = parent()->property("DICOM servers").toStringList();

    if (serverNames.isEmpty())
    {
        QSettings settings;
        settings.beginGroup("servers");
        serverNames = settings.allKeys();
        settings.endGroup();
    }

    auto server = cb->currentText();
    cb->clear();
    cb->addItems(serverNames);
    cb->setCurrentIndex(cb->findText(server));
}

void DicomMppsMwlSettings::onUseToggle(bool checked)
{
    auto check = static_cast<QCheckBox*>(sender());
    auto cb = check == checkUseMpps? cbMppsServer: cbMwlServer;

    cb->setEnabled(checked);
    if (!checked)
    {
        cb->setCurrentIndex(-1);
    }
}

void DicomMppsMwlSettings::save()
{
    QSettings settings;
    settings.setValue("mwl-server", checkUseMwl->isChecked()? cbMwlServer->currentText(): nullptr);
    settings.setValue("mpps-server", checkUseMpps->isChecked()? cbMppsServer->currentText(): nullptr);
    settings.setValue("start-with-mpps", checkStartWithMpps->isChecked());
    settings.setValue("complete-with-mpps", checkCompleteWithMpps->isChecked());
    settings.setValue("translate-cyrillic", checkTransCyr->isChecked());
}
