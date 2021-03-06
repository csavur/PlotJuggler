#include "datastream_lsl.h"
#include "ui_datastream_lsl.h"

#include <QMessageBox>
#include <QDebug>
#include <QDialog>

StreamLSLDialog::StreamLSLDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DataStreamLSL)
{
    ui->setupUi(this);

    ui->tableView->setModel(&model);

    resolveLSLStreams();
}

StreamLSLDialog::~StreamLSLDialog()
{
    delete ui;
}

QStringList StreamLSLDialog::getSelectedStreams()
{
    QStringList selected_streams;

    QModelIndexList list =ui->tableView->selectionModel()->selectedRows(0);


    foreach(const QModelIndex &index, list){
        selected_streams.append(index.data(Qt::DisplayRole).toString());
        qDebug() << "Selected Indexes : " << index.data(Qt::DisplayRole).toString();
    }

    return selected_streams;
}

void StreamLSLDialog::resolveLSLStreams()
{
    std::vector<lsl::stream_info> streams = lsl::resolve_streams();
    QStringList stream_list;

    ui->tableView->horizontalHeader()->setBackgroundRole(QPalette::NoRole);

    const int NUM_COL = 3;

    model.setRowCount(int(streams.size()));
    model.setColumnCount(NUM_COL);

    model.setHeaderData(0, Qt::Horizontal, tr("ID"));
    model.setHeaderData(1, Qt::Horizontal, tr("Name"));
    model.setHeaderData(2, Qt::Horizontal, tr("Type"));

    ui->tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

    for (int i = 0; i < streams.size(); ++i) {

        lsl::stream_info info = streams.at(i);

        stream_list.append(QString::fromStdString(info.name()));

        model.setItem(i, 0, new QStandardItem(QString::fromStdString(info.source_id())));
        model.setItem(i, 1, new QStandardItem(QString::fromStdString(info.name())));
        model.setItem(i, 2, new QStandardItem(QString::fromStdString(info.type())));
    }
}

DataStreamLSL::DataStreamLSL():
    _running(false)
{
}

DataStreamLSL::~DataStreamLSL()
{
    shutdown();
}

bool DataStreamLSL::start(QStringList*)
{
    if (_running) {
        return _running;
    }

    StreamLSLDialog* dialog = new StreamLSLDialog();

    int res = dialog->exec();

    QStringList selection = dialog->getSelectedStreams();

    if(res == QDialog::Rejected || selection.empty())
    {
        _running = false;
        return false;
    }

    _running = true;

    for (QString name: selection) {
        Streamer *streamer = new Streamer;
        if(!streamer->queryStream(name)) {
            delete streamer;
            continue;
        }

        QThread *thread = new QThread(this);
        thread->setObjectName("Streamer Thread...");

        _streams.insert(thread, streamer);

        streamer->moveToThread(thread);
        connect(thread, &QThread::started, streamer, &Streamer::stream);
        connect(streamer, &Streamer::dataReceived, this, &DataStreamLSL::dataReceived, Qt::QueuedConnection);
        connect(thread, &QThread::finished, streamer, &Streamer::deleteLater);

        thread->start();
    }

    dialog->deleteLater();
    return _running;
}

void DataStreamLSL::shutdown()
{
    if(_running) {
        _running = false;
        for (QThread *t : _streams.keys()) {
            t->requestInterruption();
            t->quit();
            t->wait(100);
        }
        _running = false;
    }
}

void DataStreamLSL::dataReceived(std::vector<std::vector<double> > *chunk, std::vector<double> *stamps)
{
    Streamer *streamer = qobject_cast<Streamer*>(sender());

    if (!streamer || (stamps->size() > 0))
    {
        //    qInfo() << chunk->size() << chunk->at(0).size();
        std::lock_guard<std::mutex> lock( mutex() );
        std::vector<std::string> channel_names = streamer->channelList();
        for (unsigned int i = 0; i < channel_names.size(); ++i) {

            std::string ch = channel_names[i];
            auto index_it = dataMap().numeric.find(ch);

            if( index_it == dataMap().numeric.end()) {
                index_it = dataMap().addNumeric(ch);
            }

            for (unsigned int j = 0; j < chunk->size(); ++j) {
                index_it->second.pushBack(PJ::PlotData::Point( stamps->at(j), chunk->at(j).at(i)));
            }
        }
    }
}

