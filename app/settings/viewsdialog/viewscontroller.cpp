/*
 * Copyright 2021  Michail Vourlakos <mvourlakos@gmail.com>
 *
 * This file is part of Latte-Dock
 *
 * Latte-Dock is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * Latte-Dock is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "viewscontroller.h"

// local
#include "ui_viewsdialog.h"
#include "viewsdialog.h"
#include "viewshandler.h"
#include "viewsmodel.h"
#include "viewstableview.h"
#include "delegates/namedelegate.h"
#include "delegates/singleoptiondelegate.h"
#include "delegates/singletextdelegate.h"
#include "../generic/generictools.h"
#include "../settingsdialog/templateskeeper.h"
#include "../../layout/centrallayout.h"
#include "../../layouts/manager.h"
#include "../../layouts/synchronizer.h"

// Qt
#include <QHeaderView>
#include <QItemSelection>

// KDE
#include <KMessageWidget>


namespace Latte {
namespace Settings {
namespace Controller {


Views::Views(Settings::Handler::ViewsHandler *parent)
    : QObject(parent),
      m_handler(parent),
      m_model(new Model::Views(this, m_handler->corona())),
      m_proxyModel(new QSortFilterProxyModel(this)),
      m_view(m_handler->ui()->viewsTable),
      m_storage(KConfigGroup(KSharedConfig::openConfig(),"LatteSettingsDialog").group("ViewsDialog"))
{
    loadConfig();
    m_proxyModel->setSourceModel(m_model);

    connect(m_model, &QAbstractItemModel::dataChanged, this, &Views::dataChanged);
    connect(m_model, &Model::Views::rowsInserted, this, &Views::dataChanged);
    connect(m_model, &Model::Views::rowsRemoved, this, &Views::dataChanged);

    connect(m_handler, &Handler::ViewsHandler::currentLayoutChanged, this, &Views::onCurrentLayoutChanged);

    init();
}

Views::~Views()
{
    saveConfig();
}

QAbstractItemModel *Views::proxyModel() const
{
    return m_proxyModel;
}

QAbstractItemModel *Views::baseModel() const
{
    return m_model;
}

QTableView *Views::view() const
{
    return m_view;
}

void Views::init()
{
    m_view->setModel(m_proxyModel);
    //m_view->setHorizontalHeader(m_headerView);
    m_view->verticalHeader()->setVisible(false);
    m_view->setSortingEnabled(true);

    m_proxyModel->setSortRole(Model::Views::SORTINGROLE);
    m_proxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);

    m_view->sortByColumn(m_viewSortColumn, m_viewSortOrder);

    m_view->setItemDelegateForColumn(Model::Views::IDCOLUMN, new Settings::View::Delegate::SingleText(this));
    m_view->setItemDelegateForColumn(Model::Views::NAMECOLUMN, new Settings::View::Delegate::NameDelegate(this));
    m_view->setItemDelegateForColumn(Model::Views::SCREENCOLUMN, new Settings::View::Delegate::SingleOption(this));
    m_view->setItemDelegateForColumn(Model::Views::EDGECOLUMN, new Settings::View::Delegate::SingleOption(this));
    m_view->setItemDelegateForColumn(Model::Views::ALIGNMENTCOLUMN, new Settings::View::Delegate::SingleOption(this));
    m_view->setItemDelegateForColumn(Model::Views::SUBCONTAINMENTSCOLUMN, new Settings::View::Delegate::SingleText(this));

    applyColumnWidths();

    m_cutAction = new QAction(QIcon::fromTheme("edit-cut"), i18n("Cut"), m_view);
    m_cutAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_X));
    connect(m_cutAction, &QAction::triggered, this, &Views::cutSelectedViews);

    m_copyAction = new QAction(QIcon::fromTheme("edit-copy"), i18n("Copy"), m_view);
    m_copyAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_C));
    connect(m_copyAction, &QAction::triggered, this, &Views::copySelectedViews);

    m_pasteAction = new QAction(QIcon::fromTheme("edit-paste"), i18n("Paste"), m_view);
    m_pasteAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_V));
    connect(m_pasteAction, &QAction::triggered, this, &Views::pasteSelectedViews);

    m_duplicateAction = new QAction(QIcon::fromTheme("edit-copy"), i18n("Duplicate Here"), m_view);
    m_duplicateAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_D));
    connect(m_duplicateAction, &QAction::triggered, this, &Views::duplicateSelectedViews);

    m_view->addAction(m_cutAction);
    m_view->addAction(m_copyAction);
    m_view->addAction(m_duplicateAction);
    m_view->addAction(m_pasteAction);

    onSelectionsChanged();

    connect(m_view, &View::ViewsTableView::selectionsChanged, this, &Views::onSelectionsChanged);
    connect(m_view, &QObject::destroyed, this, &Views::storeColumnWidths);

    connect(m_view->horizontalHeader(), &QObject::destroyed, this, [&]() {
        m_viewSortColumn = m_view->horizontalHeader()->sortIndicatorSection();
        m_viewSortOrder = m_view->horizontalHeader()->sortIndicatorOrder();
    });
}

void Views::reset()
{
    m_model->resetData();

    //! Clear any templates keeper data in order to produce reupdates if needed
    m_handler->layoutsController()->templatesKeeper()->clear();
}

bool Views::hasChangedData() const
{
    return m_model->hasChangedData();
}

bool Views::hasSelectedView() const
{
    return m_view->selectionModel()->hasSelection();
}

int Views::rowForId(QString id) const
{
    for (int i = 0; i < m_proxyModel->rowCount(); ++i) {
        QString rowId = m_proxyModel->data(m_proxyModel->index(i, Model::Views::IDCOLUMN), Qt::UserRole).toString();

        if (rowId == id) {
            return i;
        }
    }

    return -1;
}

const Data::ViewsTable Views::selectedViewsCurrentData() const
{
    Data::ViewsTable selectedviews;

    if (!hasSelectedView()) {
        return selectedviews;
    }

    QModelIndexList layoutidindexes = m_view->selectionModel()->selectedRows(Model::Views::IDCOLUMN);

    for(int i=0; i<layoutidindexes.count(); ++i) {
        QString selectedid = layoutidindexes[i].data(Qt::UserRole).toString();
        selectedviews <<  m_model->currentData(selectedid);
    }

    return selectedviews;
}

const Latte::Data::View Views::appendViewFromViewTemplate(const Data::View &view)
{
    Data::View newview = view;
    newview.name = uniqueViewName(view.name);
    m_model->appendTemporaryView(newview);
    return newview;
}

Data::ViewsTable Views::selectedViewsForClipboard()
{
    Data::ViewsTable clipboardviews;
    if (!hasSelectedView()) {
        return clipboardviews;
    }

    Data::ViewsTable selectedviews = selectedViewsCurrentData();
    Latte::Data::Layout currentlayout = m_handler->currentData();

    for(int i=0; i<selectedviews.rowCount(); ++i) {
        if (selectedviews[i].state() == Data::View::IsCreated) {
            QString storedviewpath = m_handler->layoutsController()->templatesKeeper()->storedView(currentlayout.id, selectedviews[i].id);
            Latte::Data::View copiedview = selectedviews[i];
            copiedview.setState(Data::View::OriginFromLayout, storedviewpath, currentlayout.id, selectedviews[i].id);
            copiedview.isActive = false;
            clipboardviews << copiedview;
        } else if (selectedviews[i].state() == Data::View::OriginFromViewTemplate
                   || selectedviews[i].state() == Data::View::OriginFromLayout) {
            Latte::Data::View copiedview = selectedviews[i];
            copiedview.isActive = false;
            clipboardviews << copiedview;
        }
    }

    return clipboardviews;
}

void Views::copySelectedViews()
{
    qDebug() << Q_FUNC_INFO;

    if (!hasSelectedView()) {
        return;
    }

    Data::ViewsTable clipboardviews = selectedViewsForClipboard();
    m_handler->layoutsController()->templatesKeeper()->setClipboardContents(clipboardviews);
}

void Views::cutSelectedViews()
{
    qDebug() << Q_FUNC_INFO;

    if (!hasSelectedView()) {
        return;
    }

    Data::ViewsTable clipboardviews = selectedViewsForClipboard();

    for (int i=0; i<clipboardviews.rowCount(); ++i) {
        clipboardviews[i].isMoveOrigin = true;

        Data::View tempview = m_model->currentData(clipboardviews[i].id);
        tempview.isMoveOrigin = true;
        m_model->updateCurrentView(tempview.id, tempview);
    }

    m_handler->layoutsController()->templatesKeeper()->setClipboardContents(clipboardviews);
}

void Views::pasteSelectedViews()
{
    Data::ViewsTable clipboardviews = m_handler->layoutsController()->templatesKeeper()->clipboardContents();

    for(int i=0; i<clipboardviews.rowCount(); ++i) {
        appendViewFromViewTemplate(clipboardviews[i]);
    }
}

void Views::duplicateSelectedViews()
{
    qDebug() << Q_FUNC_INFO;

    if (!hasSelectedView()) {
        return;
    }

    Data::ViewsTable selectedviews = selectedViewsCurrentData();
    Latte::Data::Layout currentlayout = m_handler->currentData();

    for(int i=0; i<selectedviews.rowCount(); ++i) {
        if (selectedviews[i].state() == Data::View::IsCreated) {
            QString storedviewpath = m_handler->layoutsController()->templatesKeeper()->storedView(currentlayout.id, selectedviews[i].id);
            Latte::Data::View duplicatedview = selectedviews[i];
            duplicatedview.setState(Data::View::OriginFromLayout, storedviewpath, currentlayout.id, selectedviews[i].id);
            duplicatedview.isActive = false;
            appendViewFromViewTemplate(duplicatedview);
        } else if (selectedviews[i].state() == Data::View::OriginFromViewTemplate
                   || selectedviews[i].state() == Data::View::OriginFromLayout) {
            Latte::Data::View duplicatedview = selectedviews[i];
            duplicatedview.isActive = false;
            appendViewFromViewTemplate(duplicatedview);
        }
    }
}

void Views::removeSelectedViews()
{
    if (!hasSelectedView()) {
        return;
    }

    Data::ViewsTable selectedviews = selectedViewsCurrentData();;

    int selectionheadrow = m_model->rowForId(selectedviews[0].id);

    for (int i=0; i<selectedviews.rowCount(); ++i) {
        m_model->removeView(selectedviews[i].id);
    }

    m_view->selectRow(qBound(0, selectionheadrow, m_model->rowCount()-1));
}

void Views::selectRow(const QString &id)
{
    m_view->selectRow(rowForId(id));
}

void Views::onCurrentLayoutChanged()
{   
    Data::Layout layout = m_handler->currentData();
    m_model->setOriginalData(layout.views);
}

void Views::onSelectionsChanged()
{
    bool hasselectedview = hasSelectedView();

    m_cutAction->setVisible(hasselectedview);
    m_copyAction->setVisible(hasselectedview);
    m_duplicateAction->setVisible(hasselectedview);
    m_pasteAction->setEnabled(m_handler->layoutsController()->templatesKeeper()->hasClipboardContents());
}

int Views::viewsForRemovalCount() const
{
    if (!hasChangedData()) {
        return 0;
    }

    Latte::Data::ViewsTable originalViews = m_model->originalViewsData();
    Latte::Data::ViewsTable currentViews = m_model->currentViewsData();
    Latte::Data::ViewsTable removedViews = originalViews.subtracted(currentViews);

    return removedViews.rowCount();
}

void Views::save()
{
    //! when this function is called we consider that removal has already been approved

    Latte::Data::Layout originallayout = m_handler->originalData();
    Latte::Data::Layout currentlayout = m_handler->currentData();
    Latte::CentralLayout *centralActive = m_handler->isSelectedLayoutOriginal() ? m_handler->corona()->layoutsManager()->synchronizer()->centralLayout(originallayout.name) : nullptr;
    Latte::CentralLayout *central = centralActive ? centralActive : new Latte::CentralLayout(this, currentlayout.id);

    //! views in model
    Latte::Data::ViewsTable originalViews = m_model->originalViewsData();
    Latte::Data::ViewsTable currentViews = m_model->currentViewsData();
    Latte::Data::ViewsTable alteredViews = m_model->alteredViews();
    Latte::Data::ViewsTable newViews = m_model->newViews();

    QHash<QString, Data::View> newviewsresponses;
    QHash<QString, Data::View> cuttedviews;

    //! add new views that are accepted
    for(int i=0; i<newViews.rowCount(); ++i){
        if (newViews[i].isMoveOrigin) {
            cuttedviews[newViews[i].id] = newViews[i];
            continue;
        }

        if (newViews[i].state() == Data::View::OriginFromViewTemplate) {
            Data::View addedview = central->newView(newViews[i]);

            newviewsresponses[newViews[i].id] = addedview;
        } else if (newViews[i].state() == Data::View::OriginFromLayout) {
            Data::View adjustedview = newViews[i];
            adjustedview.setState(Data::View::OriginFromViewTemplate, newViews[i].originFile(), QString(), QString());
            Data::View addedview = central->newView(adjustedview);

            newviewsresponses[newViews[i].id] = addedview;
        }
    }

    //! update altered views
    for (int i=0; i<alteredViews.rowCount(); ++i) {
        if (alteredViews[i].state() == Data::View::IsCreated && !alteredViews[i].isMoveOrigin) {
            qDebug() << "org.kde.latte updating altered view :: " << alteredViews[i];
            central->updateView(alteredViews[i]);
        }

        if (alteredViews[i].isMoveOrigin) {
            cuttedviews[alteredViews[i].id] = alteredViews[i];
        }
    }

    //! remove deprecated views that have been removed from user
    Latte::Data::ViewsTable removedViews = originalViews.subtracted(currentViews);

    for (int i=0; i<removedViews.rowCount(); ++i) {
        central->removeView(removedViews[i]);
    }

    //! remove deprecated views that have been removed from Cut operation
    for(const auto vid: cuttedviews.keys()){
        if (cuttedviews[vid].state() == Data::View::IsCreated) {
            central->removeView(cuttedviews[vid]);
        }
    }

    //! update
    if ((removedViews.rowCount() > 0) || (newViews.rowCount() > 0)) {
        m_handler->corona()->layoutsManager()->synchronizer()->syncActiveLayoutsToOriginalFiles();
    }

    //! update model for newly added views
    for (const auto vid: newviewsresponses.keys()) {
        m_model->setOriginalView(vid, newviewsresponses[vid]);
    }

    //! update/remove from model cutted views
    for (const auto vid: cuttedviews.keys()) {
        m_model->removeView(vid);
    }

    //! update all table with latest data and make the original one
    currentViews = m_model->currentViewsData();
    m_model->setOriginalData(currentViews);

    //! update model activeness
    if (central->isActive()) {
        m_model->updateActiveStatesBasedOn(central);
    }

    //! Clear any templates keeper data in order to produce reupdates if needed
    m_handler->layoutsController()->templatesKeeper()->clear();
}

QString Views::uniqueViewName(QString name)
{
    if (name.isEmpty()) {
            return name;
    }

    int pos_ = name.lastIndexOf(QRegExp(QString(" - [0-9]+")));

    if (m_model->containsCurrentName(name) && pos_ > 0) {
        name = name.left(pos_);
    }

    int i = 2;

    QString namePart = name;

    while (m_model->containsCurrentName(name)) {
        name = namePart + " - " + QString::number(i);
        i++;
    }

    return name;
}

void Views::applyColumnWidths()
{
    m_view->horizontalHeader()->setSectionResizeMode(Model::Views::NAMECOLUMN, QHeaderView::Stretch);

    if (m_viewColumnWidths.count()<(Model::Views::columnCount()-1)) {
        return;
    }

    m_view->setColumnWidth(Model::Views::IDCOLUMN, m_viewColumnWidths[0].toInt());
    m_view->setColumnWidth(Model::Views::SCREENCOLUMN, m_viewColumnWidths[1].toInt());
    m_view->setColumnWidth(Model::Views::EDGECOLUMN, m_viewColumnWidths[2].toInt());
    m_view->setColumnWidth(Model::Views::ALIGNMENTCOLUMN, m_viewColumnWidths[3].toInt());
    m_view->setColumnWidth(Model::Views::SUBCONTAINMENTSCOLUMN, m_viewColumnWidths[4].toInt());
}

void Views::storeColumnWidths()
{
    if (m_viewColumnWidths.isEmpty() || (m_viewColumnWidths.count()<Model::Views::columnCount()-1)) {
        m_viewColumnWidths.clear();
        for (int i=0; i<Model::Views::columnCount(); ++i) {
            m_viewColumnWidths << "";
        }
    }

    m_viewColumnWidths[0] = QString::number(m_view->columnWidth(Model::Views::IDCOLUMN));
    m_viewColumnWidths[1] = QString::number(m_view->columnWidth(Model::Views::SCREENCOLUMN));
    m_viewColumnWidths[2] = QString::number(m_view->columnWidth(Model::Views::EDGECOLUMN));
    m_viewColumnWidths[3] = QString::number(m_view->columnWidth(Model::Views::ALIGNMENTCOLUMN));
    m_viewColumnWidths[4] = QString::number(m_view->columnWidth(Model::Views::SUBCONTAINMENTSCOLUMN));
}

void Views::loadConfig()
{
    m_viewColumnWidths = m_storage.readEntry("columnWidths", QStringList());
    m_viewSortColumn = m_storage.readEntry("sortColumn", (int)Model::Views::SCREENCOLUMN);
    m_viewSortOrder = static_cast<Qt::SortOrder>(m_storage.readEntry("sortOrder", (int)Qt::AscendingOrder));
}

void Views::saveConfig()
{
    m_storage.writeEntry("columnWidths", m_viewColumnWidths);
    m_storage.writeEntry("sortColumn", m_viewSortColumn);
    m_storage.writeEntry("sortOrder", (int)m_viewSortOrder);
}

}
}
}
