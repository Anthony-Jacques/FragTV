﻿#include "TcpClient.h"
#include "clientWindow.h"
#include "FragEnums.h"
#include "WidgetFlash.h"

#include <QtNetwork>
#include <QByteArray>

TcpClient::TcpClient(QObject* parent) : QObject(parent), blockSize(0)
{
    tcpSocket = new QTcpSocket(this);
    tcpSocket->setReadBufferSize(1024 * 1024);
    tcpSocket->setSocketOption(QAbstractSocket::KeepAliveOption, true);


    connect(CLIENTUI->streamConnectButton    , SIGNAL(clicked())                           , this , SLOT(tcpConnect())                           );
    connect(CLIENTUI->streamDisconnectButton , SIGNAL(clicked())                           , this , SLOT(tcpDisconnect())                        );

    connect(tcpSocket                        , SIGNAL(readyRead())                         , this , SLOT(tcpReadData())                          );
    connect(tcpSocket                        , SIGNAL(error(QAbstractSocket::SocketError)) , this , SLOT(tcpError(QAbstractSocket::SocketError)) );
    connect(tcpSocket                        , SIGNAL(connected())                         , this , SLOT(tcpConnected())                         );
    connect(tcpSocket                        , SIGNAL(disconnected())                      , this , SLOT(tcpDisconnected())                      );
}
TcpClient::~TcpClient() 
{
    tcpSocket->abort();
}

void TcpClient::tcpConnect()
{
    QString serverName   = CLIENTUI->serverNameField->text();
    QString serverString = CLIENTUI->serverAddressField->text(); 
    QString hostIp       = serverString.split(":").value(0); 
    int hostPort         = serverString.split(":").value(1).toInt();


    CLIENTUI->networkStatusField->setText("Connecting to: " + serverName);
    WidgetFlash widgetFlash;
    widgetFlash.green(CLIENTUI->networkStatusField);

    qDebug() << "Connecting to: " << serverName << " " << serverString;


    tcpSocket->abort();
    blockSize = 0;

    tcpSocket->connectToHost(hostIp, hostPort);
}
void TcpClient::tcpConnected()
{
    CLIENTUI->networkStatusField->setText("Connected!");
    WidgetFlash widgetFlash;
    widgetFlash.green(CLIENTUI->networkStatusField);

    CLIENTUI->motdTextBrowser->clear();
    CLIENTUI->tabWidget->setCurrentWidget(CLIENTUI->motdTab);

    qDebug() << "Connected!";
}

void TcpClient::tcpDisconnect()
{
    CLIENTUI->networkStatusField->setText("Disconnecting..");
    WidgetFlash widgetFlash;
    widgetFlash.green(CLIENTUI->networkStatusField);

    qDebug() << "Disconnecting..";


    tcpSocket->disconnectFromHost();
}
void TcpClient::tcpDisconnected()
{
    CLIENTUI->tabWidget->setCurrentIndex(0);


    CLIENTUI->motdTextBrowser->clear();


    CLIENTUI->networkStatusField->setText("Disconnected!");
    WidgetFlash widgetFlash;
    widgetFlash.red(CLIENTUI->networkStatusField);

    qDebug() << "Disconnected!";
}

void TcpClient::tcpReadData()
{
    QDataStream dataStream(tcpSocket);
    dataStream.setVersion(QDataStream::Qt_4_0);

    if(blockSize == 0) 
    {
        if(tcpSocket->bytesAvailable() < (int)sizeof(quint32)) // Download 4 bytes minimum before proceeding
            return;

        dataStream >> blockSize; // Take the first 4 bytes as size
    }

    if(tcpSocket->bytesAvailable() < blockSize) // Wait until we have received the complete message
        return;


    quint32 fragSignature; 
    quint32 compression;
    quint32 serverVersion;
    quint32 minimumVersion;
    int flags = 4;

    int messageSizeFinal = blockSize - (flags * 4); // Discard the size of flags and calculate the final message size
    blockSize = 0; // reset for next message 


    if(messageSizeFinal <= 0)
    {
        qDebug() << "Tcp unknown error, messageSizeFinal <= 0";

        tcpSocket->abort();
        return;
    }


    // The QDataStream::operator>> overload for quint32 will only read 4 bytes

    dataStream >> fragSignature  ; // Take the next 4 bytes as FragTV server signature
    dataStream >> compression    ; // Take the next 4 bytes as compression flag
    dataStream >> serverVersion  ; // Take the next 4 bytes as version number
    dataStream >> minimumVersion ; // Take the next 4 bytes as minimum version number


    if(fragSignature != FRAGTV::Socket::Signature)
    {
        qDebug() << "Not a FragTV Server! Disconnecting";

        tcpSocket->abort();
        return;
    }


    bool isMsgCompressed = false;

    if(compression == FRAGTV::Socket::Compressed)
    {
        isMsgCompressed = true;
    }


    if(minimumVersion > FRAGTV::Socket::Version)
    {
        qDebug() << "FragTV version outdated, cannot connect to server, minimumVersion: " << minimumVersion;

        emit this->incompatibleVersion(minimumVersion);


        tcpSocket->abort();
        return;
    }


    QByteArray message;
    message.resize(messageSizeFinal);
    
    dataStream.readRawData(message.data(), messageSizeFinal);


    emit this->newMessage(isMsgCompressed, message);


    if(tcpSocket->bytesAvailable() > 0)
    {
        // There is unread data remaining in the buffer, 
        // manually call tcpReadData to process the next message.
        // This is required because readyRead is only fired once
        // for each burst, even if it contains multiple messages
        QMetaObject::invokeMethod(this, "tcpReadData", Qt::QueuedConnection);
    }
}

void TcpClient::tcpError(QAbstractSocket::SocketError socketError)
{
    Q_UNUSED(socketError);
    
    
    CLIENTUI->networkStatusField->setText(tcpSocket->errorString());
    WidgetFlash widgetFlash;
    widgetFlash.red(CLIENTUI->networkStatusField);

    qDebug() << tcpSocket->errorString();
}

