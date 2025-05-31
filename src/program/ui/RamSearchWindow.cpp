/*
    Copyright 2015-2024 Clément Gallet <clement.gallet@ens-lyon.org>

    This file is part of libTAS.

    libTAS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    libTAS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with libTAS.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "RamSearchWindow.h"
#include "RamSearchModel.h"
#include "RamWatchView.h"
#include "RamWatchWindow.h"
#include "RamWatchEditWindow.h"
#include "HexViewWindow.h"

#include "Context.h"
#include "ramsearch/CompareOperations.h"
#include "ramsearch/MemScannerThread.h" // error codes

#include <QtWidgets/QTableView>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QMessageBox>
#include <QtGui/QFontDatabase>

#include <limits>
#include <thread>

RamSearchWindow::RamSearchWindow(Context* c, HexViewWindow* view, RamWatchWindow* ram, QWidget *parent) : QDialog(parent), context(c), hexViewWindow(view), ramWatchWindow(ram)
{
    setWindowTitle("Ram Search");

    /* Get monospace font */
    const QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    /* Table */
    ramSearchView = new QTableView(this);
    ramSearchView->setSelectionBehavior(QAbstractItemView::SelectRows);
    ramSearchView->setSelectionMode(QAbstractItemView::SingleSelection);
    ramSearchView->setShowGrid(false);
    ramSearchView->setAlternatingRowColors(true);
    ramSearchView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ramSearchView->horizontalHeader()->setHighlightSections(false);
    ramSearchView->verticalHeader()->hide();
    ramSearchView->setFont(fixedFont);

    ramSearchModel = new RamSearchModel(context);
    ramSearchView->setModel(ramSearchModel);

    /* Progress bar */
    searchProgress = new QProgressBar();
    connect(&ramSearchModel->memscanner, &MemScanner::signalProgress, searchProgress, &QProgressBar::setValue);

    watchCount = new QLabel();
    // watchCount->setHeight(searchProgress->height());

    /* Buttons */
    newButton = new QPushButton(tr("New"));
    connect(newButton, &QAbstractButton::clicked, this, &RamSearchWindow::slotNew);

    searchButton = new QPushButton(tr("Search"));
    connect(searchButton, &QAbstractButton::clicked, this, &RamSearchWindow::slotSearch);
    searchButton->setDisabled(true);

    stopButton = new QPushButton(tr("Force Stop"));
    connect(stopButton, &QAbstractButton::clicked, this, &RamSearchWindow::slotStop);
    stopButton->setDisabled(true);

    QPushButton *addButton = new QPushButton(tr("Add Watch"));
    connect(addButton, &QAbstractButton::clicked, this, &RamSearchWindow::slotAdd);

    QPushButton *hexButton = new QPushButton(tr("Hex View"));
    connect(hexButton, &QAbstractButton::clicked, this, &RamSearchWindow::slotHex);

    QDialogButtonBox *buttonBox = new QDialogButtonBox();
    buttonBox->addButton(newButton, QDialogButtonBox::ActionRole);
    buttonBox->addButton(searchButton, QDialogButtonBox::ActionRole);
    buttonBox->addButton(stopButton, QDialogButtonBox::ActionRole);
    buttonBox->addButton(addButton, QDialogButtonBox::ActionRole);
    buttonBox->addButton(hexButton, QDialogButtonBox::ActionRole);

    QVBoxLayout *watchLayout = new QVBoxLayout;
    watchLayout->addWidget(ramSearchView);
    watchLayout->addWidget(searchProgress);
    watchLayout->addWidget(watchCount);
    watchLayout->addWidget(buttonBox);

    /* Memory regions */
    memSpecialBox = new QCheckBox("Exclude special regions");
    memSpecialBox->setChecked(true);
    memROBox = new QCheckBox("Exclude read-only regions");
    memROBox->setChecked(true);
    memExecBox = new QCheckBox("Exclude executable regions");
    memExecBox->setChecked(true);
    memBeginLine = new QLineEdit("0000000000000000");
    memEndLine = new QLineEdit("00007fffffffffff");
    memBeginLine->setMaxLength(16);
    memEndLine->setMaxLength(16);
    memBeginLine->setFont(fixedFont);
    memEndLine->setFont(fixedFont);

    memGroupBox = new QGroupBox(tr("Included Memory Flags"));
    QGridLayout *memLayout = new QGridLayout;
    memLayout->addWidget(memSpecialBox, 0, 0, 1, 2);
    memLayout->addWidget(memROBox, 1, 0, 1, 2);
    memLayout->addWidget(memExecBox, 2, 0, 1, 2);
    memLayout->addWidget(new QLabel(tr("Start:")), 3, 0, 1, 1);
    memLayout->addWidget(memBeginLine, 3, 1, 1, 1);
    memLayout->addWidget(new QLabel(tr("Stop:")), 4, 0, 1, 1);
    memLayout->addWidget(memEndLine, 4, 1, 1, 1);
    memGroupBox->setLayout(memLayout);

    /* Comparisons */
    comparePreviousButton = new QRadioButton("Unknown/Previous Value");
    comparePreviousButton->setChecked(true);
    compareValueButton = new QRadioButton("Specific Value:");
    comparingValueBox = new QLineEdit();
    comparingValueBox->setFont(fixedFont);

    QGroupBox *compareGroupBox = new QGroupBox(tr("Compare To"));
    QVBoxLayout *compareLayout = new QVBoxLayout;
    compareLayout->addWidget(comparePreviousButton);
    compareLayout->addWidget(compareValueButton);
    compareLayout->addWidget(comparingValueBox);
    compareGroupBox->setLayout(compareLayout);

    /* Operators */
    operatorEqualButton = new QRadioButton("Equal To");
    operatorEqualButton->setChecked(true);
    operatorNotEqualButton = new QRadioButton("Not Equal To");
    operatorLessButton = new QRadioButton("Less Than");
    operatorGreaterButton = new QRadioButton("Greater Than");
    operatorLessEqualButton = new QRadioButton("Less Than Or Equal To");
    operatorGreaterEqualButton = new QRadioButton("Greater Than Or Equal To");
    operatorDifferenceButton = new QRadioButton("Different By");
    differenceValueBox = new QLineEdit();
    differenceValueBox->setFont(fixedFont);

    QGroupBox *operatorGroupBox = new QGroupBox(tr("Comparison Operator"));
    QGridLayout *operatorLayout = new QGridLayout;
    operatorLayout->addWidget(operatorEqualButton, 0, 0, 1, 2);
    operatorLayout->addWidget(operatorNotEqualButton, 1, 0, 1, 2);
    operatorLayout->addWidget(operatorLessButton, 2, 0, 1, 2);
    operatorLayout->addWidget(operatorGreaterButton, 3, 0, 1, 2);
    operatorLayout->addWidget(operatorLessEqualButton, 4, 0, 1, 2);
    operatorLayout->addWidget(operatorGreaterEqualButton, 5, 0, 1, 2);
    operatorLayout->addWidget(operatorDifferenceButton, 6, 0);
    operatorLayout->addWidget(differenceValueBox, 6, 1);
    operatorGroupBox->setLayout(operatorLayout);

    /* Format */
    typeBox = new QComboBox();
    QStringList typeList;
    typeList << "unsigned char" << "char" << "unsigned short" << "short";
    typeList << "unsigned int" << "int" << "unsigned int64" << "int64";
    typeList << "float" << "double" << "byte array" << "string";
    typeBox->addItems(typeList);
    typeBox->setCurrentText("int");
    connect(typeBox, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, &RamSearchWindow::slotTypeChanged);

    displayBox = new QComboBox();
    displayBox->addItem("decimal");
    displayBox->addItem("hexadecimal");

    alignmentBox = new QComboBox();
    alignmentBox->addItem("default", 0);
    alignmentBox->addItem("1", 1);
    alignmentBox->addItem("2", 2);
    alignmentBox->addItem("4", 4);
    alignmentBox->setCurrentIndex(0);

    formatGroupBox = new QGroupBox(tr("Format"));
    QFormLayout *formatLayout = new QFormLayout;
    formatLayout->addRow(new QLabel(tr("Type:")), typeBox);
    formatLayout->addRow(new QLabel(tr("Display:")), displayBox);
    formatLayout->addRow(new QLabel(tr("Alignment:")), alignmentBox);
    formatGroupBox->setLayout(formatLayout);

    /* Create the options layout */
    QVBoxLayout *optionLayout = new QVBoxLayout;
    optionLayout->addWidget(memGroupBox);
    optionLayout->addWidget(compareGroupBox);
    optionLayout->addWidget(operatorGroupBox);
    optionLayout->addWidget(formatGroupBox);

    QHBoxLayout *mainLayout = new QHBoxLayout;

    mainLayout->addLayout(watchLayout, 1);
    mainLayout->addLayout(optionLayout);

    setLayout(mainLayout);
    
    /* Start the update timer */
    updateTimer = new QElapsedTimer();
    updateTimer->start();

    /* Configure the call timer */
    callTimer = new QTimer(this);
    callTimer->setSingleShot(true);
    connect(callTimer, &QTimer::timeout, this, &RamSearchWindow::update);
    
    isSearching = false;
}

void RamSearchWindow::update()
{
    /* Only update on new frame and every .5 ms */
    int64_t elapsed = updateTimer->elapsed();
    if (elapsed < 500) {
        /* Call this function on timeout, if not already done */
        if (!callTimer->isActive()) {
            callTimer->start(500 - elapsed);
        }
        return;
    }
    updateTimer->start();

    ramSearchModel->update();
}

void RamSearchWindow::getCompareParameters(CompareType& compare_type, CompareOperator& compare_operator, MemValueType& compare_value, MemValueType& different_value)
{
    compare_type = CompareType::Previous;
    if (compareValueButton->isChecked()) {
        compare_type = CompareType::Value;
        compare_value = MemValue::from_string(qPrintable(comparingValueBox->text()), typeBox->currentIndex(), false);
    }

    compare_operator = CompareOperator::Equal;
    if (operatorNotEqualButton->isChecked())
        compare_operator = CompareOperator::NotEqual;
    if (operatorLessButton->isChecked())
        compare_operator = CompareOperator::Less;
    if (operatorGreaterButton->isChecked())
        compare_operator = CompareOperator::Greater;
    if (operatorLessEqualButton->isChecked())
        compare_operator = CompareOperator::LessEqual;
    if (operatorGreaterEqualButton->isChecked())
        compare_operator = CompareOperator::GreaterEqual;
    if (operatorDifferenceButton->isChecked()) {
        compare_operator = CompareOperator::Different;
        different_value = MemValue::from_string(qPrintable(differenceValueBox->text()), typeBox->currentIndex(), false);
    }
}

void RamSearchWindow::slotNew()
{
    if (isSearching)
        return;
        
    if (context->status != Context::ACTIVE)
        return;
    
    /* If there are results, then clear the current scan and enable all boxes */
    if (ramSearchModel->scanSize() > 0) {
        newButton->setText(tr("New"));
        memGroupBox->setDisabled(false);
        formatGroupBox->setDisabled(false);
        ramSearchModel->clear();
        watchCount->setText("");
        searchProgress->reset();
        searchButton->setDisabled(true);
        return;
    }

    isSearching = true;

    /* Disable buttons during the process */
    newButton->setDisabled(true);
    searchButton->setDisabled(true);
    stopButton->setDisabled(false);

    /* Build the memory region flag variable */
    int memflags = 0;
    if (memSpecialBox->isChecked())
        memflags |= MemSection::MemNoSpecial;
    if (memROBox->isChecked())
        memflags |= MemSection::MemNoRO;
    if (memExecBox->isChecked())
        memflags |= MemSection::MemNoExec;

    searchProgress->reset();
    searchProgress->setMaximum(ramSearchModel->predictScanCount(memflags));

    /* Start the actual scan search on a thread */
    std::thread t(&RamSearchWindow::threadedNew, this, memflags);
    t.detach();
}

void RamSearchWindow::threadedNew(int memflags)
{
    /* Get the comparison parameters */
    CompareType compare_type;
    CompareOperator compare_operator;
    MemValueType compare_value;
    MemValueType different_value;
    getCompareParameters(compare_type, compare_operator, compare_value, different_value);

    ramSearchModel->hex = (displayBox->currentIndex() == 1);
    int alignment = alignmentBox->currentData().toInt();

    uintptr_t begin_address = std::strtoul(qPrintable(memBeginLine->text()), nullptr, 16);
    uintptr_t end_address = std::strtoul(qPrintable(memEndLine->text()), nullptr, 16);

    /* Call the RamSearch new function using the right type */
    int err = ramSearchModel->newWatches(memflags, typeBox->currentIndex(), alignment, compare_type, compare_operator, compare_value, different_value, begin_address, end_address);

    if (err < 0)
        searchProgress->reset();

    switch (err) {
        case MemScannerThread::ESTOPPED:
            watchCount->setText(tr("The search was interupted by the user"));
            break;
        case MemScannerThread::EOUTPUT:
            watchCount->setText(tr("The search results could not be written to disk"));
            break;
        case MemScannerThread::EINPUT:
            watchCount->setText(tr("The previous search results could not be read correctly"));
            break;
        case MemScannerThread::EPROCESS:
            watchCount->setText(tr("There was an error in the search process"));
            break;
        default:
            /* Don't display values if too many results */
            if ((ramSearchModel->memscanner.display_scan_count() == 0) && (ramSearchModel->scanCount() != 0))
                watchCount->setText(QString("%1 addresses (results are not shown above %2)").arg(ramSearchModel->scanCount()).arg(ramSearchModel->memscanner.DISPLAY_THRESHOLD));
            else
                watchCount->setText(QString("%1 addresses").arg(ramSearchModel->scanCount()));
            break;
    }

    /* Change the button to "Stop" and disable some boxes */
    if (ramSearchModel->scanCount() != 0 || err < 0) {
        newButton->setText(tr("Stop"));
        memGroupBox->setDisabled(true);
        formatGroupBox->setDisabled(true);
    }
    else {
        newButton->setText(tr("New"));
        memGroupBox->setDisabled(false);
        formatGroupBox->setDisabled(false);
    }
    
    newButton->setDisabled(false);
    searchButton->setDisabled(false);
    stopButton->setDisabled(true);
    
    isSearching = false;
}

void RamSearchWindow::slotSearch()
{
    if (isSearching)
        return;

    isSearching = true;

    /* Disable buttons during the process */
    newButton->setDisabled(true);
    searchButton->setDisabled(true);
    stopButton->setDisabled(false);

    searchProgress->reset();
    searchProgress->setMaximum(ramSearchModel->scanSize());

    /* Start the actual scan search on a thread */
    std::thread t(&RamSearchWindow::threadedSearch, this);
    t.detach();
}

void RamSearchWindow::threadedSearch()
{
    CompareType compare_type;
    CompareOperator compare_operator;
    MemValueType compare_value;
    MemValueType different_value;
    getCompareParameters(compare_type, compare_operator, compare_value, different_value);

    int err = ramSearchModel->searchWatches(compare_type, compare_operator, compare_value, different_value);

    if (err < 0)
        searchProgress->reset();

    switch (err) {
        case MemScannerThread::ESTOPPED:
            watchCount->setText(tr("The search was interupted by the user"));
            break;
        case MemScannerThread::EOUTPUT:
            watchCount->setText(tr("The search results could not be written to disk"));
            break;
        case MemScannerThread::EINPUT:
            watchCount->setText(tr("The previous search results could not be read correctly"));
            break;
        case MemScannerThread::EPROCESS:
            watchCount->setText(tr("There was an error in the search process"));
            break;
        default:
            /* Don't display values if too many results */
            if ((ramSearchModel->memscanner.display_scan_count() == 0) && (ramSearchModel->scanCount() != 0))
                watchCount->setText(QString("%1 addresses (results are not shown above %2)").arg(ramSearchModel->scanCount()).arg(ramSearchModel->memscanner.DISPLAY_THRESHOLD));
            else
                watchCount->setText(QString("%1 addresses").arg(ramSearchModel->scanCount()));
            break;
    }

    /* Change the button to "New" if no results */
    if (ramSearchModel->scanCount() == 0 || err < 0) {
        newButton->setText(tr("New"));
        memGroupBox->setDisabled(false);
        formatGroupBox->setDisabled(false);
        searchButton->setDisabled(true);
    }
    
    newButton->setDisabled(false);
    searchButton->setDisabled(false);
    stopButton->setDisabled(true);

    isSearching = false;
}

void RamSearchWindow::slotAdd()
{
    const QModelIndex index = ramSearchView->selectionModel()->currentIndex();
    ramSearchView->selectionModel()->clear();

    /* If no watch was selected, return */
    if (!index.isValid()) {
        QMessageBox::critical(nullptr, "Error", QString("You must select an address to add a watch"));
        return;
    }

    int row = index.row();

    /* Fill the watch edit window with parameters from the selected watch */
    ramWatchWindow->ramWatchView->editWindow->fill(ramSearchModel->address(row), typeBox->currentIndex());
    ramWatchWindow->ramWatchView->slotAdd();
}

void RamSearchWindow::slotHex()
{
    const QModelIndex index = ramSearchView->selectionModel()->currentIndex();

    /* If no watch was selected, return */
    if (!index.isValid())
        return;

    hexViewWindow->seek(ramSearchModel->address(index.row()), MemValue::type_size(typeBox->currentIndex()));
    hexViewWindow->show();
}

void RamSearchWindow::slotStop()
{
    ramSearchModel->stopSearch();
}

void RamSearchWindow::slotTypeChanged(int index)
{
    if (index == RamArray || index == RamCString) {
        compareValueButton->setChecked(true);
        comparePreviousButton->setEnabled(false);
        operatorEqualButton->setChecked(true);
        operatorNotEqualButton->setEnabled(false);
        operatorLessButton->setEnabled(false);
        operatorGreaterButton->setEnabled(false);
        operatorLessEqualButton->setEnabled(false);
        operatorGreaterEqualButton->setEnabled(false);
        operatorDifferenceButton->setEnabled(false);
    }
    else {
        comparePreviousButton->setEnabled(true);
        operatorNotEqualButton->setEnabled(true);
        operatorLessButton->setEnabled(true);
        operatorGreaterButton->setEnabled(true);
        operatorLessEqualButton->setEnabled(true);
        operatorGreaterEqualButton->setEnabled(true);
        operatorDifferenceButton->setEnabled(true);
    }
}
