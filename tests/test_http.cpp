#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QDebug>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>

class HttpTester : public QObject
{
    Q_OBJECT

public:
    HttpTester(QObject *parent = nullptr) : QObject(parent)
    {
        manager = new QNetworkAccessManager(this);
        connect(manager, &QNetworkAccessManager::finished, this, &HttpTester::onReplyFinished);
    }

    void testRequest()
    {
        QString url = "http://localhost/api/files.php";
        qDebug() << "Testing URL:" << url;
        
        QNetworkRequest request(QUrl(url));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("User-Agent", "Qt HTTP Tester");
        
        QNetworkReply *reply = manager->get(request);
        
        // Add timeout
        QTimer::singleShot(10000, this, [this, reply]() {
            if (reply->isRunning()) {
                qDebug() << "Request timed out";
                reply->abort();
                QCoreApplication::quit();
            }
        });
    }

private slots:
    void onReplyFinished(QNetworkReply *reply)
    {
        qDebug() << "Request finished";
        qDebug() << "Error:" << reply->error();
        qDebug() << "Error string:" << reply->errorString();
        qDebug() << "HTTP status:" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            qDebug() << "Response size:" << data.size() << "bytes";
            qDebug() << "Response content:" << QString::fromUtf8(data.left(500)); // First 500 chars
            
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (!doc.isNull()) {
                qDebug() << "JSON parsed successfully";
                if (doc.isObject()) {
                    QJsonObject obj = doc.object();
                    qDebug() << "Success field:" << obj["success"].toBool();
                    if (obj.contains("folders")) {
                        qDebug() << "Folders array size:" << obj["folders"].toArray().size();
                    }
                }
            } else {
                qDebug() << "Failed to parse JSON";
            }
        }
        
        reply->deleteLater();
        QCoreApplication::quit();
    }

private:
    QNetworkAccessManager *manager;
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    
    HttpTester tester;
    tester.testRequest();
    
    return app.exec();
}

#include "test_http.moc"
