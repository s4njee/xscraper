#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QUrl>
#include <QVector>
#include <functional>

class QNetworkReply;

class ScrapeController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString outputPath READ outputPath NOTIFY outputPathChanged)
    Q_PROPERTY(int postsCount READ postsCount NOTIFY postsCountChanged)
    Q_PROPERTY(int mediaTotal READ mediaTotal NOTIFY mediaProgressChanged)
    Q_PROPERTY(int mediaCompleted READ mediaCompleted NOTIFY mediaProgressChanged)
    Q_PROPERTY(int mediaFailed READ mediaFailed NOTIFY mediaProgressChanged)
    Q_PROPERTY(QString currentMedia READ currentMedia NOTIFY mediaProgressChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    explicit ScrapeController(QObject *parent = nullptr);

    bool running() const { return m_running; }
    QString status() const { return m_status; }
    QString outputPath() const { return m_outputPath; }
    int postsCount() const { return m_posts.size(); }
    int mediaTotal() const { return m_totalDownloads; }
    int mediaCompleted() const { return m_completedDownloads; }
    int mediaFailed() const { return m_failedDownloads; }
    QString currentMedia() const { return m_currentMedia; }
    QString error() const { return m_error; }

    Q_INVOKABLE void start(const QString &username,
                           const QString &bearerToken,
                           const QString &outputDirectory,
                           bool excludeReplies,
                           bool excludeReposts);
    Q_INVOKABLE void importArchive(const QString &archiveDirectory,
                                   const QString &outputDirectory,
                                   bool excludeReplies,
                                   bool excludeReposts);
    Q_INVOKABLE void processExport(const QString &exportJsonPath,
                                   const QString &outputDirectory);
    Q_INVOKABLE void cancel();

signals:
    void runningChanged();
    void statusChanged();
    void outputPathChanged();
    void postsCountChanged();
    void mediaProgressChanged();
    void errorChanged();
    void finished(bool ok);

private:
    void reset();
    void setRunning(bool running);
    void setStatus(const QString &status);
    void setOutputPath(const QString &path);
    void setError(const QString &error);

    QNetworkRequest request(const QUrl &url) const;
    void lookupUser();
    void fetchNextPage(const QString &paginationToken = {});
    void scheduleNextPage(const QString &paginationToken = {});
    void handleUserReply(QNetworkReply *reply);
    void handleTimelineReply(QNetworkReply *reply);
    void retryAfterRateLimit(const QString &what, int delayMs, const std::function<void()> &retry);
    bool importArchiveFiles(const QString &archiveDirectory);
    void prepareExportProcessing(const QJsonObject &exportRoot);
    bool writePostsText(const QJsonObject &exportRoot);
    bool writePostsHtml(const QJsonObject &exportRoot);
    void downloadNextMedia();
    void handleMediaReply(QNetworkReply *reply, const QString &path);
    void finishOk();
    void finishWithError(const QString &message);
    bool writeExport();

    static QJsonArray valuesFromMap(const QJsonObject &object);
    static QJsonArray linkedContentFromPost(const QJsonObject &post);
    static QJsonDocument archiveJsDocument(const QString &path, QString *error);
    static QJsonObject normalizedArchivePost(const QJsonObject &tweet);
    static QJsonArray archiveMediaFromPost(const QJsonObject &tweet);
    static bool isReplyPost(const QJsonObject &post);
    static bool isRepostPost(const QJsonObject &post);
    static QString exportUsername(const QJsonObject &exportRoot);
    static QString postUrl(const QJsonObject &post, const QString &username);
    static QString displayText(const QJsonObject &post);
    static QString linkifyText(const QJsonObject &post);
    static QString formatTimestamp(const QString &raw);
    static QString sanitizeFilePart(QString text);
    static QString extensionForUrl(const QUrl &url, const QString &fallback);
    static QJsonArray mediaItemsForPost(const QJsonObject &post, const QJsonObject &mediaByKey);
    static QUrl bestMediaUrl(const QJsonObject &media);

    struct MediaDownload {
        QUrl url;
        QString path;
        QString postId;
    };

    QNetworkAccessManager m_network;
    QNetworkReply *m_activeReply = nullptr;
    QString m_username;
    QString m_bearerToken;
    QString m_outputDirectory;
    QString m_userId;
    QString m_currentPaginationToken;
    QString m_source = QStringLiteral("X API v2 user posts timeline");
    QJsonObject m_user;
    QJsonArray m_posts;
    QJsonObject m_mediaByKey;
    QJsonObject m_usersById;
    QJsonObject m_placesById;
    QJsonObject m_pollsById;
    QJsonArray m_rawPages;
    QVector<MediaDownload> m_pendingDownloads;
    int m_totalDownloads = 0;
    int m_completedDownloads = 0;
    int m_failedDownloads = 0;
    QString m_currentMedia;
    bool m_excludeReplies = true;
    bool m_excludeReposts = true;
    bool m_running = false;
    QString m_status;
    QString m_outputPath;
    QString m_error;
};
