#include "logsdialog.h"
#include "appdata.h"
#include "ui_logsdialog.h"
#include "helpers.h"
#if defined WIN32 || !defined __GNUC__
#include <windows.h>
#else
#include <unistd.h>
#endif

logsDialog::logsDialog(QWidget *parent) :
    QDialog(parent, Qt::WindowTitleHint | Qt::WindowSystemMenuHint),
    ui(new Ui::logsDialog)
{
  csvlog.clear();

  ui->setupUi(this);
  this->setWindowIcon(CompanionIcon("logs.png"));

  plotLock=false;

  colors.append(Qt::green);
  colors.append(Qt::red);
  colors.append(Qt::yellow);
  colors.append(Qt::magenta);
  colors.append(Qt::cyan);
  colors.append(Qt::darkBlue);
  colors.append(Qt::darkGreen);
  colors.append(Qt::darkRed);
  colors.append(Qt::darkYellow);
  colors.append(Qt::darkMagenta);
  colors.append(Qt::darkCyan);
  colors.append(Qt::blue);
  pen.setWidthF(1.0);

  // create and prepare a plot title layout element
  QCPPlotTitle *title = new QCPPlotTitle(ui->customPlot);
  title->setText(tr("Telemetry logs"));
  // title->setFont(QFont("sans", 12, QFont::Bold));
  // add it to the main plot layout
  ui->customPlot->plotLayout()->insertRow(0);
  ui->customPlot->plotLayout()->addElement(0, 0, title);

  ui->customPlot->setNoAntialiasingOnDrag(true);

  ui->customPlot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom |
    QCP::iSelectAxes | QCP::iSelectLegend | QCP::iSelectPlottables);

  ui->customPlot->xAxis->setLabel(tr("Time (hh:mm:ss)"));
  ui->customPlot->xAxis->setTickLabelType(QCPAxis::ltDateTime);
  ui->customPlot->xAxis->setDateTimeFormat("hh:mm:ss");
  QDateTime now = QDateTime::currentDateTime();
  ui->customPlot->xAxis->setRange(now.addSecs(-60*60*2).toTime_t(),
    now.toTime_t());

  ui->customPlot->yAxis->setTickLabels(false);

  QFont legendFont = font();
  legendFont.setPointSize(10);
  ui->customPlot->legend->setFont(legendFont);
  ui->customPlot->legend->setSelectedFont(legendFont);

  QString Path=g.gePath();
  if (Path.isEmpty() || !QFile(Path).exists()) {
    ui->mapsButton->hide();
  }

  // connect slot that ties some axis selections together (especially opposite axes):
  connect(ui->customPlot, SIGNAL(selectionChangedByUser()), this, SLOT(selectionChanged()));
  // connect slots that takes care that when an axis is selected, only that direction can be dragged and zoomed:
  connect(ui->customPlot, SIGNAL(mousePress(QMouseEvent*)), this, SLOT(mousePress()));
  connect(ui->customPlot, SIGNAL(mouseWheel(QWheelEvent*)), this, SLOT(mouseWheel()));

  // make left axes transfer its range to right axes:
  connect(ui->customPlot->yAxis, SIGNAL(rangeChanged(QCPRange)), this, SLOT(setRangeyAxis2(QCPRange)));

  // connect some interaction slots:
  connect(ui->customPlot, SIGNAL(titleDoubleClick(QMouseEvent*, QCPPlotTitle*)), this, SLOT(titleDoubleClick(QMouseEvent*, QCPPlotTitle*)));
  connect(ui->customPlot, SIGNAL(axisDoubleClick(QCPAxis*,QCPAxis::SelectablePart,QMouseEvent*)), this, SLOT(axisLabelDoubleClick(QCPAxis*,QCPAxis::SelectablePart)));
  connect(ui->customPlot, SIGNAL(legendDoubleClick(QCPLegend*,QCPAbstractLegendItem*,QMouseEvent*)), this, SLOT(legendDoubleClick(QCPLegend*,QCPAbstractLegendItem*)));
  connect(ui->FieldsTW, SIGNAL(itemSelectionChanged()), this, SLOT(plotLogs()));
  connect(ui->logTable, SIGNAL(itemSelectionChanged()), this, SLOT(plotLogs()));
  connect(ui->Reset_PB, SIGNAL(clicked()), this, SLOT(plotLogs()));
}

logsDialog::~logsDialog()
{
  delete ui;
}

void logsDialog::titleDoubleClick(QMouseEvent *evt, QCPPlotTitle *title)
{
  // Set the plot title by double clicking on it

  bool ok;
  QString newTitle = QInputDialog::getText(this, tr("Plot Title Change"),
    tr("New plot title:"), QLineEdit::Normal, title->text(), &ok);
  if (ok)
  {
    title->setText(newTitle);
    ui->customPlot->replot();
  }
}

void logsDialog::axisLabelDoubleClick(QCPAxis *axis, QCPAxis::SelectablePart part)
{
  // Set an axis label by double clicking on it
  if (part == QCPAxis::spAxisLabel) // only react when the actual axis label is clicked, not tick label or axis backbone
  {
    bool ok;
    QString newLabel = QInputDialog::getText(this, tr("Axis Label Change"), tr("New axis label:"), QLineEdit::Normal, axis->label(), &ok);
    if (ok)
    {
      axis->setLabel(newLabel);
      ui->customPlot->replot();
    }
  }
}

void logsDialog::legendDoubleClick(QCPLegend *legend, QCPAbstractLegendItem *item)
{
  // Rename a graph by double clicking on its legend item
  if (item) // only react if item was clicked (user could have clicked on border padding of legend where there is no item, then item is 0)
  {
    QCPPlottableLegendItem *plItem = qobject_cast<QCPPlottableLegendItem*>(item);
    bool ok;
    QString newName = QInputDialog::getText(this, tr("Graph Name Change"), tr("New graph name:"), QLineEdit::Normal, plItem->plottable()->name(), &ok);
    if (ok)
    {
      plItem->plottable()->setName(newName);
      ui->customPlot->replot();
    }
  }
}

void logsDialog::selectionChanged()
{
  /*
   normally, axis base line, axis tick labels and axis labels are selectable separately, but we want
   the user only to be able to select the axis as a whole, so we tie the selected states of the tick labels
   and the axis base line together. However, the axis label shall be selectable individually.

   The selection state of the left and right axes shall be synchronized as well as the state of the
   bottom and top axes.

   Further, we want to synchronize the selection of the graphs with the selection state of the respective
   legend item belonging to that graph. So the user can select a graph by either clicking on the graph itself
   or on its legend item.
  */

  if (plotLock) return;

  // handle bottom axis and tick labels as one selectable object:
  if (ui->customPlot->xAxis->selectedParts().testFlag(QCPAxis::spAxis) ||
    ui->customPlot->xAxis->selectedParts().testFlag(QCPAxis::spTickLabels))
  {
    ui->customPlot->xAxis->setSelectedParts(QCPAxis::spAxis |
      QCPAxis::spTickLabels);
  }
  // make left and right axes be selected synchronously,
  // and handle axis and tick labels as one selectable object:
  if (ui->customPlot->yAxis->selectedParts().testFlag(QCPAxis::spAxis) ||
    ui->customPlot->yAxis->selectedParts().testFlag(QCPAxis::spTickLabels) ||
    ui->customPlot->yAxis2->selectedParts().testFlag(QCPAxis::spAxis) ||
    ui->customPlot->yAxis2->selectedParts().testFlag(QCPAxis::spTickLabels))
  {
    ui->customPlot->yAxis->setSelectedParts(QCPAxis::spAxis |
      QCPAxis::spTickLabels);
    if (hasyAxis2) {
      ui->customPlot->yAxis2->setSelectedParts(QCPAxis::spAxis |
        QCPAxis::spTickLabels);
    }
  }

  // synchronize selection of graphs with selection of corresponding legend items:
  for (int i=0; i<ui->customPlot->graphCount(); ++i)
  {
    QCPGraph *graph = ui->customPlot->graph(i);
    QCPPlottableLegendItem *item = ui->customPlot->legend->itemWithPlottable(graph);
    if (item->selected() || graph->selected())
    {
      item->setSelected(true);
      graph->setSelected(true);
    }
  }
}

void logsDialog::on_mapsButton_clicked()
{
  int n = csvlog.count(); // number of points in graph
  if (n==0) return;
  int latcol=0, longcol=0, altcol=0, speedcol=0;
  int itemSelected=0.;
  bool rangeSelected=false;
  ui->FieldsTW->setDisabled(true);
  ui->logTable->setDisabled(true);

  QString gePath=g.gePath();
  if (gePath.isEmpty() || !QFile(gePath).exists()) {
    ui->FieldsTW->setDisabled(false);
    ui->logTable->setDisabled(false);
    return;
  }
  QSet<int> nondataCols;
  for (int i=1; i<csvlog.at(0).count(); i++) {
    //Long,Lat,Course,GPS Speed,GPS Alt
    if (csvlog.at(0).at(i).contains("Long")) {
      longcol=i;
      nondataCols << i;
    }
    if (csvlog.at(0).at(i).contains("Lat")) {
      latcol=i;
      nondataCols << i;
    }
    if (csvlog.at(0).at(i).contains("GPS Alt")) {
      altcol=i;
      nondataCols << i;
    }
    if (csvlog.at(0).at(i).contains("GPS Speed")) {
      speedcol=i;
      nondataCols << i;
    }
  }
  if (longcol==0 || latcol==0 || altcol==0) {
    return;
  }
  for (int i=1; i<n; i++) {
    if (ui->logTable->item(i-1,1)->isSelected()) {
      rangeSelected=true;
      itemSelected++;
    }
  }
  if (itemSelected==0) {
    itemSelected=n-1;
  }

  QString geIconFilename = generateProcessUniqueTempFileName("track0.png");
  if (QFile::exists(geIconFilename)) {
    QFile::remove(geIconFilename);
  }
  QFile::copy(":/images/track0.png", geIconFilename);

  QString geFilename = generateProcessUniqueTempFileName("flight.kml");
  if (QFile::exists(geFilename)) {
    QFile::remove(geFilename);
  }
  QFile geFile(geFilename);
  if (!geFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QMessageBox::warning(this, tr("Error"),
        tr("Cannot write file %1:\n%2.")
        .arg(geFilename)
        .arg(geFile.errorString()));
    ui->FieldsTW->setDisabled(false);
    ui->logTable->setDisabled(false);
    return;
  }
  QString latitude,longitude;
  double flatitude, flongitude,temp;
  QTextStream outputStream(&geFile);
  outputStream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<kml xmlns=\"http://www.opengis.net/kml/2.2\" xmlns:gx=\"http://www.google.com/kml/ext/2.2\">\n";
  outputStream << "\t<Document>\n\t\t<name>" << logFilename << "</name>\n";
  outputStream << "\t\t<Style id=\"multiTrack_n\">\n\t\t\t<IconStyle>\n\t\t\t\t<Icon>\n\t\t\t\t\t<href>file://" << geIconFilename << "</href>\n\t\t\t\t</Icon>\n\t\t\t</IconStyle>\n\t\t\t<LineStyle>\n\t\t\t\t<color>991081f4</color>\n\t\t\t\t<width>6</width>\n\t\t\t</LineStyle>\n\t\t</Style>\n";
  outputStream << "\t\t<Style id=\"multiTrack_h\">\n\t\t\t<IconStyle>\n\t\t\t\t<scale>0</scale>\n\t\t\t\t<Icon>\n\t\t\t\t\t<href>file://" << QDir::tempPath() << "/track0.png</href>\n\t\t\t\t</Icon>\n\t\t\t</IconStyle>\n\t\t\t<LineStyle>\n\t\t\t\t<color>991081f4</color>\n\t\t\t\t<width>8</width>\n\t\t\t</LineStyle>\n\t\t</Style>\n";
  outputStream << "\t\t<StyleMap id=\"multiTrack\">\n\t\t\t<Pair>\n\t\t\t\t<key>normal</key>\n\t\t\t\t<styleUrl>#multiTrack_n</styleUrl>\n\t\t\t</Pair>\n\t\t\t<Pair>\n\t\t\t\t<key>highlight</key>\n\t\t\t\t<styleUrl>#multiTrack_h</styleUrl>\n\t\t\t</Pair>\n\t\t</StyleMap>\n";
  outputStream << "\t\t<Style id=\"lineStyle\">\n\t\t\t<LineStyle>\n\t\t\t\t<color>991081f4</color>\n\t\t\t\t<width>6</width>\n\t\t\t</LineStyle>\n\t\t</Style>\n";
  outputStream << "\t\t<Schema id=\"schema\">\n";
  outputStream << "\t\t\t<gx:SimpleArrayField name=\"GPSSpeed\" type=\"float\">\n\t\t\t\t<displayName>GPS Speed</displayName>\n\t\t\t</gx:SimpleArrayField>\n";
  // declare additional fields
  for (int i=0; i<csvlog.at(0).count()-2; i++) {
    if (ui->FieldsTW->item(0,i)->isSelected() && !nondataCols.contains(i+2)) {
      QString origName = csvlog.at(0).at(i+2);
      QString safeName = origName;
      safeName.replace(" ","_");
      outputStream << "\t\t\t<gx:SimpleArrayField name=\""<< safeName <<"\" ";
      outputStream << "type=\"string\"";   // additional fields have fixed type: string
      outputStream << ">\n\t\t\t\t<displayName>" << origName << "</displayName>\n\t\t\t</gx:SimpleArrayField>\n";
    }
  }
  outputStream << "\t\t</Schema>\n";

  QString planeName;
  if (logFilename.indexOf("-")>0) {
    planeName=logFilename.left(logFilename.indexOf("-"));
  } else {
    planeName=logFilename;
  }
  outputStream << "\t\t<Folder>\n\t\t\t<name>Log Data</name>\n\t\t\t<Placemark>\n\t\t\t\t<name>" << planeName << "</name>";
  outputStream << "\n\t\t\t\t<styleUrl>#multiTrack</styleUrl>";
  outputStream << "\n\t\t\t\t<gx:Track>\n";
  outputStream << "\n\t\t\t\t\t<altitudeMode>absolute</altitudeMode>\n";
  for (int i=1; i<n; i++) {
    if ((ui->logTable->item(i-1,1)->isSelected() &&rangeSelected) || !rangeSelected) {
      QString tstamp=csvlog.at(i).at(0)+QString("T")+csvlog.at(i).at(1)+QString("Z");
      outputStream << "\t\t\t\t\t<when>"<< tstamp <<"</when>\n";
    }
  }

  for (int i=1; i<n; i++) {
    if ((ui->logTable->item(i-1,1)->isSelected() &&rangeSelected) || !rangeSelected) {
      latitude=csvlog.at(i).at(latcol).trimmed();
      longitude=csvlog.at(i).at(longcol).trimmed();
      temp=int(latitude.left(latitude.length()-1).toDouble()/100);
      flatitude=temp+(latitude.left(latitude.length()-1).toDouble()-temp*100)/60.0;
      temp=int(longitude.left(longitude.length()-1).toDouble()/100);
      flongitude=temp+(longitude.left(longitude.length()-1).toDouble()-temp*100)/60.0;
      if (latitude.right(1)!="N") {
        flatitude*=-1;
      }
      if (longitude.right(1)!="E") {
        flongitude*=-1;
      }
      latitude.sprintf("%3.8f", flatitude);
      longitude.sprintf("%3.8f", flongitude);
      outputStream << "\t\t\t\t\t<gx:coord>" << longitude << " " << latitude << " " << csvlog.at(i).at(altcol).toFloat() << " </gx:coord>\n" ;
    }
  }
  outputStream << "\t\t\t\t\t<ExtendedData>\n\t\t\t\t\t\t<SchemaData schemaUrl=\"#schema\">\n";
  outputStream << "\t\t\t\t\t\t\t<gx:SimpleArrayData name=\"GPSSpeed\">\n";
  for (int i=1; i<n; i++) {
    if ((ui->logTable->item(i-1,1)->isSelected() &&rangeSelected) || !rangeSelected) {
      outputStream << "\t\t\t\t\t\t\t\t<gx:value>"<< csvlog.at(i).at(speedcol) <<"</gx:value>\n";
    }
  }
  outputStream << "\t\t\t\t\t\t\t</gx:SimpleArrayData>\n";
  // add values for additional fields
  for (int i=0; i<csvlog.at(0).count()-2; i++) {
    if (ui->FieldsTW->item(0,i)->isSelected() && !nondataCols.contains(i+2)) {
      QString safeName = csvlog.at(0).at(i+2);;
      safeName.replace(" ","_");
      outputStream << "\t\t\t\t\t\t\t<gx:SimpleArrayData name=\""<< safeName <<"\">\n";
      for (int j=1; j<n; j++) {
        if ((ui->logTable->item(j-1,1)->isSelected() &&rangeSelected) || !rangeSelected) {
          outputStream << "\t\t\t\t\t\t\t\t<gx:value>"<< csvlog.at(j).at(i+2) <<"</gx:value>\n";
        }
      }
      outputStream << "\t\t\t\t\t\t\t</gx:SimpleArrayData>\n";
    }
  }
  outputStream << "\t\t\t\t\t\t</SchemaData>\n\t\t\t\t\t</ExtendedData>\n\t\t\t\t</gx:Track>\n\t\t\t</Placemark>\n\t\t</Folder>\n\t</Document>\n</kml>";
  geFile.close();
  QStringList parameters;
#ifdef __APPLE__
  parameters << "-a";
  parameters << gePath;
  gePath = "/usr/bin/open";
#endif
  parameters << geFilename;
  QProcess *process = new QProcess(this);
  process->start(gePath, parameters);
  ui->FieldsTW->setDisabled(false);
  ui->logTable->setDisabled(false);
}


void logsDialog::mousePress()
{
  // if an axis is selected, only allow the direction of that axis to be dragged
  // if no axis is selected, both directions may be dragged

  if (ui->customPlot->xAxis->selectedParts().testFlag(QCPAxis::spAxis))
    ui->customPlot->axisRect()->setRangeDrag(ui->customPlot->xAxis->orientation());
  else if (ui->customPlot->yAxis->selectedParts().testFlag(QCPAxis::spAxis))
    ui->customPlot->axisRect()->setRangeDrag(ui->customPlot->yAxis->orientation());
  else
    ui->customPlot->axisRect()->setRangeDrag(Qt::Horizontal | Qt::Vertical);
}

void logsDialog::mouseWheel()
{
  // if an axis is selected, only allow the direction of that axis to be zoomed
  // if no axis is selected, both directions may be zoomed
  int orientation=0;
  if (ui->ZoomX_ChkB->isChecked()) {
    orientation|=Qt::Horizontal;
  }
  if (ui->ZoomY_ChkB->isChecked()) {
    orientation|=Qt::Vertical;
  }
  if (orientation) {
    ui->customPlot->axisRect()->setRangeZoom((Qt::Orientation)orientation);
  } else {
    ui->customPlot->axisRect()->setRangeZoom(Qt::Horizontal|Qt::Vertical);
  }
}

void logsDialog::removeSelectedGraph()
{
  if (ui->customPlot->selectedGraphs().size() > 0)
  {
    ui->customPlot->removeGraph(ui->customPlot->selectedGraphs().first());
    ui->customPlot->replot();
  }
}

void logsDialog::removeAllGraphs()
{
  ui->customPlot->clearGraphs();
  ui->customPlot->legend->setVisible(false);
  ui->customPlot->yAxis2->setVisible(false);
  ui->customPlot->yAxis->setTickLabels(false);
  ui->customPlot->replot();
}

void logsDialog::on_fileOpen_BT_clicked()
{
  QString fileName = QFileDialog::getOpenFileName(this,tr("Select your log file"), g.logDir());
  if (!fileName.isEmpty()) {

    g.logDir( fileName );
    ui->FileName_LE->setText(fileName);
    if (cvsFileParse()) {
      ui->FieldsTW->clear();
      ui->logTable->clear();
      ui->FieldsTW->setShowGrid(false);
      ui->FieldsTW->setContentsMargins(0,0,0,0);
      ui->FieldsTW->setRowCount(csvlog.at(0).count()-2);
      ui->FieldsTW->setColumnCount(1);
      ui->FieldsTW->setHorizontalHeaderLabels(QStringList(tr("Available fields")));
      ui->logTable->setSelectionBehavior(QAbstractItemView::SelectRows);
      for (int i=2; i<csvlog.at(0).count(); i++) {
        QTableWidgetItem* item= new QTableWidgetItem(csvlog.at(0).at(i));
        ui->FieldsTW->setItem(0,i-2,item);
      }
      ui->FieldsTW->resizeRowsToContents();
      ui->logTable->setColumnCount(csvlog.at(0).count());
      ui->logTable->setRowCount(csvlog.count()-1);
      ui->logTable->setHorizontalHeaderLabels(csvlog.at(0));

      QAbstractItemModel *model = ui->logTable->model();
      for (int i=1; i<csvlog.count(); i++) {
        for (int j=0; j<csvlog.at(0).count(); j++) {
          model->setData(model->index(i - 1, j, QModelIndex()), csvlog.at(i).at(j));
        }
      }

      ui->logTable->horizontalHeader()->setResizeMode(QHeaderView::ResizeToContents);
      QVarLengthArray<int> sizes;
      for (int i = 0; i < ui->logTable->columnCount(); i++) {
        sizes.append(ui->logTable->columnWidth(i));
      }
      ui->logTable->horizontalHeader()->setResizeMode(QHeaderView::Interactive);
      for (int i = 0; i < ui->logTable->columnCount(); i++) {
        ui->logTable->setColumnWidth(i, sizes.at(i));
      }
    }
  }
}

bool logsDialog::cvsFileParse()
{
  QFile file(ui->FileName_LE->text());
  int errors=0;
  int lines=-1;

  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) { //reading HEX TEXT file
    return false;
  } else {
    csvlog.clear();
    logFilename.clear();
    QTextStream inputStream(&file);
    QString buffer = file.readLine();

    if (buffer.startsWith("Date,Time")) {
      file.reset();
    } else {
      return false;
    }

    int numfields=-1;
    while (!file.atEnd()) {
      QString line = file.readLine().trimmed();
      QStringList columns = line.split(',');
      if (numfields==-1) {
        numfields=columns.count();
      }
      if (columns.count()==numfields) {
        csvlog.append(columns);
      } else {
        errors++;
      }
      lines++;
    }

    logFilename=QFileInfo(file.fileName()).baseName();
  }

  file.close();
  if (errors>1) {
    QMessageBox::warning(this, "Companion", tr("The selected logfile contains %1 invalid lines out of  %2 total lines").arg(errors).arg(lines));
  }

  int n=csvlog.count();
  if (n==1) {
    csvlog.clear();
    return false;
  }

  plotLock = true;

  ui->sessions_CB->clear();
  ui->sessions_CB->addItem("---");

  double lastvalue = 0;
  double tmp;
  for (int i = 1; i < n; i++) {
    QString tstamp = csvlog.at(i).at(0) + QString(" ") + csvlog.at(i).at(1);

    if (csvlog.at(i).at(1).contains(".")) {
      tmp = QDateTime::fromString(tstamp, "yyyy-MM-dd HH:mm:ss.zzz").toTime_t();
      tmp += csvlog.at(i).at(1).mid(csvlog.at(i).at(1).indexOf(".")).toDouble();
    } else {
      tmp = QDateTime::fromString(tstamp, "yyyy-MM-dd HH:mm:ss").toTime_t();
    }

    if (tmp > (lastvalue+60)) {
      ui->sessions_CB->addItem(tstamp, i - 1);
      lastvalue = tmp;
    } else {
      lastvalue = tmp;
    }
  }

  plotLock=false;

  return true;
}


void logsDialog::on_sessions_CB_currentIndexChanged(int index)
{
  if (plotLock) return;
  plotLock = true;

  ui->logTable->clearSelection();

  if (index != 0) {
    int bottom;
    if (index < ui->sessions_CB->count() - 1) {
      bottom = ui->sessions_CB->itemData(index + 1, Qt::UserRole).toInt();
    } else {
      bottom = ui->logTable->rowCount();
    }

    QModelIndex topLeft = ui->logTable->model()->index(
      ui->sessions_CB->itemData(index, Qt::UserRole).toInt(), 0 , QModelIndex());
    QModelIndex bottomRight = ui->logTable->model()->index(
      bottom - 1, ui->logTable->columnCount() - 1, QModelIndex());

    QItemSelection selection(topLeft, bottomRight);
    ui->logTable->selectionModel()->select(selection, QItemSelectionModel::Select);
  }

  plotLock = false;
  plotLogs();
}

void logsDialog::plotLogs()
{
  if (plotLock) return;

  if (!ui->FieldsTW->selectedItems().length()) {
    removeAllGraphs();
    return;
  }

  plotsCollection plots;

  QModelIndexList selection = ui->logTable->selectionModel()->selectedRows();
  int rowCount = selection.length();
  bool hasLogSelection;
  QVarLengthArray<int> selectedRows;

  if (rowCount) {
    hasLogSelection = true;
    foreach (QModelIndex index, selection) {
      selectedRows.append(index.row());
    }
    qSort(selectedRows.begin(), selectedRows.end());
  } else {
    hasLogSelection = false;
    rowCount = ui->logTable->rowCount();
  }

  plots.min_x = QDateTime::currentDateTime().toTime_t();
  plots.max_x = 0;

  foreach (QTableWidgetItem *plot, ui->FieldsTW->selectedItems()) {
    coords plotCoords;
    int plotColumn = plot->row() + 2; // Date and Time first

    plotCoords.min_y = 999999;
    plotCoords.max_y = -999999;
    plotCoords.secondRange = false;
    plotCoords.name = plot->text();

    for (int row = 0; row < rowCount; row++) {
      QTableWidgetItem *logValue;
      double y;
      double time;
      QString time_str;

      if (hasLogSelection) {
        logValue = ui->logTable->item(selectedRows.at(row), plotColumn);
        time_str = ui->logTable->item(selectedRows.at(row), 0)->text() +
          QString(" ") + ui->logTable->item(selectedRows.at(row), 1)->text();
      } else {
        logValue = ui->logTable->item(row, plotColumn);
        time_str = ui->logTable->item(row, 0)->text() + QString(" ") +
          ui->logTable->item(row, 1)->text();
      }

      y = logValue->text().toDouble();
      plotCoords.y.push_back(y);

      if (plotCoords.min_y > y) {
        plotCoords.min_y = y;
      } else if (plotCoords.max_y < y) {
        plotCoords.max_y = y;
      }

      if (time_str.contains('.')) {
        time = QDateTime::fromString(time_str, "yyyy-MM-dd HH:mm:ss.zzz")
          .toTime_t();
        time += time_str.mid(time_str.indexOf('.')).toDouble();
      } else {
        time = QDateTime::fromString(time_str, "yyyy-MM-dd HH:mm:ss")
          .toTime_t();
      }
      plotCoords.x.push_back(time);

      if (plots.min_x > time) {
        plots.min_x = time;
      } else if (plots.max_x < time) {
        plots.max_x = time;
      }
    }
    plots.coords.append(plotCoords);
  }

  plots.rangeOneMin = plots.coords.at(0).min_y;
  plots.rangeOneMax = plots.coords.at(0).max_y;
  plots.twoRanges = false;
  plots.tooManyRanges = false;

  for (int i = 1; i < plots.coords.size(); i++) {
    if (plots.tooManyRanges) {
      if (plots.coords.at(i).min_y < plots.rangeOneMin) {
        plots.rangeOneMin = plots.coords.at(i).min_y;
      }
      if (plots.coords.at(i).max_y > plots.rangeOneMax) {
        plots.rangeOneMax = plots.coords.at(i).max_y;
      }

      continue;
    }

    double actualRange = plots.rangeOneMax - plots.rangeOneMin;
    double thisRange = plots.coords.at(i).max_y - plots.coords.at(i).min_y;

    if (thisRange > actualRange * 1.3 || thisRange * 1.3 < actualRange ||
        plots.coords.at(i).min_y > plots.rangeOneMax ||
        plots.coords.at(i).max_y < plots.rangeOneMin) {
      plots.coords[i].secondRange = true;
      if (plots.twoRanges) {
        actualRange = plots.rangeTwoMax - plots.rangeTwoMin;
        if (thisRange > actualRange * 1.3 || thisRange * 1.3 < actualRange ||
            plots.coords.at(i).min_y > plots.rangeTwoMax ||
            plots.coords.at(i).max_y < plots.rangeTwoMin) {
          plots.tooManyRanges = true;
          plots.twoRanges = false;
          if (plots.rangeTwoMin < plots.rangeOneMin) {
            plots.rangeOneMin = plots.rangeTwoMin;
          }
          if (plots.rangeTwoMax > plots.rangeOneMax) {
            plots.rangeOneMax = plots.rangeTwoMax;
          }
          if (plots.coords.at(i).min_y < plots.rangeOneMin) {
            plots.rangeOneMin = plots.coords.at(i).min_y;
          }
          if (plots.coords.at(i).max_y > plots.rangeOneMax) {
            plots.rangeOneMax = plots.coords.at(i).max_y;
          }
        } else {
          if (plots.coords.at(i).min_y < plots.rangeTwoMin) {
            plots.rangeTwoMin = plots.coords.at(i).min_y;
          }
          if (plots.coords.at(i).max_y > plots.rangeTwoMax) {
            plots.rangeTwoMax = plots.coords.at(i).max_y;
          }
        }
      } else {
        plots.twoRanges = true;
        plots.rangeTwoMax = plots.coords.at(i).max_y;
        plots.rangeTwoMin = plots.coords.at(i).min_y;
      }
    } else {
      if (plots.coords.at(i).min_y < plots.rangeOneMin) {
        plots.rangeOneMin = plots.coords.at(i).min_y;
      }
      if (plots.coords.at(i).max_y > plots.rangeOneMax) {
        plots.rangeOneMax = plots.coords.at(i).max_y;
      }
    }
  }

  if (plots.twoRanges) {
    rangeRatio = (plots.rangeTwoMax - plots.rangeTwoMin) /
      (plots.rangeOneMax - plots.rangeOneMin);
    rangeyAxisMin = plots.rangeOneMin;
    rangeyAxisMax = plots.rangeOneMax;
    rangeyAxis2Min = plots.rangeTwoMin;
    rangeyAxis2Max = plots.rangeTwoMax;

    hasyAxis2 = true;
  } else {
    hasyAxis2 = false;
  }

  removeAllGraphs();

  ui->customPlot->xAxis->setRange(plots.min_x, plots.max_x);

  ui->customPlot->yAxis->setRange(plots.rangeOneMin, plots.rangeOneMax);
  ui->customPlot->yAxis->setTickLabels(true);

  if (plots.twoRanges) {
    ui->customPlot->yAxis2->setRange(plots.rangeTwoMin, plots.rangeTwoMax);
    ui->customPlot->yAxis2->setVisible(true);
  }

  for (int i = 0; i < plots.coords.size(); i++) {
    if (plots.coords.at(i).secondRange && plots.twoRanges) {
      ui->customPlot->addGraph(ui->customPlot->xAxis, ui->customPlot->yAxis2);
    } else {
      ui->customPlot->addGraph();
    }
    ui->customPlot->graph(i)->setData(plots.coords.at(i).x,
      plots.coords.at(i).y);
    pen.setColor(colors.at(i % colors.size()));
    ui->customPlot->graph(i)->setPen(pen);
    ui->customPlot->graph(i)->setName(plots.coords.at(i).name);
  }

  ui->customPlot->legend->setVisible(true);
  ui->customPlot->replot();
}

void logsDialog::setRangeyAxis2(QCPRange range)
{
  if (hasyAxis2) {
    double lowerChange = (range.lower - rangeyAxisMin) * rangeRatio;
    double upperChange = (range.upper - rangeyAxisMax) * rangeRatio;
    rangeyAxisMin = range.lower;
    rangeyAxisMax = range.upper;
    rangeyAxis2Min += lowerChange;
    rangeyAxis2Max += upperChange;

    ui->customPlot->yAxis2->setRange(rangeyAxis2Min, rangeyAxis2Max);
  }
}
