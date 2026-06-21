#include "app/ScrapeController.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonValue>
#include <QLocale>
#include <QRegularExpression>
#include <QTextStream>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QScopeGuard>
#include <QTimer>
#include <QUrlQuery>

namespace {
constexpr auto ApiBase = "https://api.x.com";
constexpr int TimelinePageDelayMs = 1000;

QString cleanUsername(QString username)
{
    username = username.trimmed();
    if (username.startsWith(u'@'))
        username.remove(0, 1);
    return username;
}

QJsonObject objectAt(const QJsonObject &root, const QString &key)
{
    const auto value = root.value(key);
    return value.isObject() ? value.toObject() : QJsonObject {};
}

QJsonArray arrayAt(const QJsonObject &root, const QString &key)
{
    const auto value = root.value(key);
    return value.isArray() ? value.toArray() : QJsonArray {};
}

void mergeById(QJsonObject &target, const QJsonArray &items, const QString &idField)
{
    for (const auto &value : items) {
        if (!value.isObject())
            continue;
        const auto object = value.toObject();
        const auto id = object.value(idField).toString();
        if (!id.isEmpty())
            target.insert(id, object);
    }
}

bool fileNameLooksTweetArchive(const QString &fileName)
{
    const auto lower = fileName.toLower();
    return lower.contains(QStringLiteral("tweet"))
        && !lower.contains(QStringLiteral("like"))
        && !lower.contains(QStringLiteral("ad-"))
        && !lower.contains(QStringLiteral("headers"));
}

int httpStatus(QNetworkReply *reply)
{
    return reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
}

int retryDelayMs(QNetworkReply *reply)
{
    bool ok = false;
    const auto retryAfter = reply->rawHeader("retry-after").trimmed().toLongLong(&ok);
    if (ok && retryAfter > 0)
        return static_cast<int>(qMin<qint64>(retryAfter * 1000, 24LL * 60 * 60 * 1000));

    const auto resetEpoch = reply->rawHeader("x-rate-limit-reset").trimmed().toLongLong(&ok);
    if (ok && resetEpoch > 0) {
        const auto now = QDateTime::currentSecsSinceEpoch();
        const auto seconds = qMax<qint64>(resetEpoch - now + 2, 5);
        return static_cast<int>(qMin<qint64>(seconds * 1000, 24LL * 60 * 60 * 1000));
    }

    return 15 * 60 * 1000;
}

QString humanDelay(int delayMs)
{
    const auto seconds = qMax(1, delayMs / 1000);
    if (seconds < 90)
        return QStringLiteral("%1 seconds").arg(seconds);
    return QStringLiteral("%1 minutes").arg((seconds + 59) / 60);
}
}

ScrapeController::ScrapeController(QObject *parent)
    : QObject(parent)
{
    setStatus(QStringLiteral("Idle"));
}

void ScrapeController::start(const QString &username,
                             const QString &bearerToken,
                             const QString &outputDirectory,
                             bool excludeReplies,
                             bool excludeReposts)
{
    if (m_running)
        return;

    reset();
    m_username = cleanUsername(username);
    m_bearerToken = bearerToken.trimmed();
    m_outputDirectory = outputDirectory.trimmed();
    m_currentPaginationToken.clear();
    m_source = QStringLiteral("X API v2 user posts timeline");
    m_excludeReplies = excludeReplies;
    m_excludeReposts = excludeReposts;

    if (m_username.isEmpty()) {
        finishWithError(QStringLiteral("Enter a username."));
        return;
    }
    if (m_bearerToken.isEmpty()) {
        finishWithError(QStringLiteral("Enter an X API bearer token."));
        return;
    }
    if (m_outputDirectory.isEmpty())
        m_outputDirectory = QDir::homePath();

    setRunning(true);
    lookupUser();
}

void ScrapeController::importArchive(const QString &archiveDirectory,
                                     const QString &outputDirectory,
                                     bool excludeReplies,
                                     bool excludeReposts)
{
    if (m_running)
        return;

    reset();
    m_outputDirectory = outputDirectory.trimmed();
    m_source = QStringLiteral("X archive import");
    m_excludeReplies = excludeReplies;
    m_excludeReposts = excludeReposts;

    if (archiveDirectory.trimmed().isEmpty()) {
        finishWithError(QStringLiteral("Select an extracted X archive folder."));
        return;
    }
    if (m_outputDirectory.isEmpty())
        m_outputDirectory = QDir::homePath();

    setRunning(true);
    setStatus(QStringLiteral("Reading archive"));
    if (!importArchiveFiles(archiveDirectory.trimmed()))
        return;

    finishOk();
}

void ScrapeController::processExport(const QString &exportJsonPath,
                                     const QString &outputDirectory)
{
    if (m_running)
        return;

    reset();
    m_source = QStringLiteral("Xscraper export post-processing");
    m_outputDirectory = outputDirectory.trimmed();
    if (m_outputDirectory.isEmpty())
        m_outputDirectory = QDir::homePath();

    if (exportJsonPath.trimmed().isEmpty()) {
        finishWithError(QStringLiteral("Select an Xscraper JSON export."));
        return;
    }

    QFile file(exportJsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        finishWithError(QStringLiteral("Could not open export JSON."));
        return;
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        finishWithError(QStringLiteral("That file is not valid export JSON."));
        return;
    }

    const auto exportRoot = document.object();
    m_posts = arrayAt(exportRoot, QStringLiteral("posts"));
    m_user = objectAt(exportRoot, QStringLiteral("user"));
    m_username = exportUsername(exportRoot);
    emit postsCountChanged();

    if (m_posts.isEmpty()) {
        finishWithError(QStringLiteral("No posts found in that export."));
        return;
    }

    setRunning(true);
    setStatus(QStringLiteral("Preparing posts.txt and media downloads"));
    prepareExportProcessing(exportRoot);
    if (!writePostsText(exportRoot))
        return;
    if (!writePostsHtml(exportRoot))
        return;

    if (m_pendingDownloads.isEmpty()) {
        setStatus(QStringLiteral("Wrote posts.txt and posts.html; no media URLs found"));
        setRunning(false);
        emit finished(true);
        return;
    }

    downloadNextMedia();
}

void ScrapeController::cancel()
{
    if (!m_running)
        return;
    if (m_activeReply)
        m_activeReply->abort();
    finishWithError(QStringLiteral("Canceled."));
}

void ScrapeController::reset()
{
    m_userId.clear();
    m_username.clear();
    m_currentPaginationToken.clear();
    m_user = {};
    m_posts = {};
    m_mediaByKey = {};
    m_usersById = {};
    m_placesById = {};
    m_pollsById = {};
    m_rawPages = {};
    m_pendingDownloads.clear();
    m_totalDownloads = 0;
    m_completedDownloads = 0;
    m_failedDownloads = 0;
    m_currentMedia.clear();
    setOutputPath({});
    setError({});
    emit postsCountChanged();
    emit mediaProgressChanged();
}

void ScrapeController::setRunning(bool running)
{
    if (m_running == running)
        return;
    m_running = running;
    emit runningChanged();
}

void ScrapeController::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

void ScrapeController::setOutputPath(const QString &path)
{
    if (m_outputPath == path)
        return;
    m_outputPath = path;
    emit outputPathChanged();
}

void ScrapeController::setError(const QString &error)
{
    if (m_error == error)
        return;
    m_error = error;
    emit errorChanged();
}

QNetworkRequest ScrapeController::request(const QUrl &url) const
{
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", "Bearer " + m_bearerToken.toUtf8());
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("xscraper/0.1"));
    return req;
}

void ScrapeController::lookupUser()
{
    setStatus(QStringLiteral("Looking up @%1").arg(m_username));

    QUrl url(QStringLiteral("%1/2/users/by/username/%2").arg(ApiBase, m_username));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("user.fields"),
                       QStringLiteral("created_at,description,entities,id,location,name,profile_image_url,protected,public_metrics,url,username,verified,verified_type"));
    url.setQuery(query);

    auto *reply = m_network.get(request(url));
    m_activeReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (m_activeReply == reply)
            m_activeReply = nullptr;
        if (!m_running) {
            reply->deleteLater();
            return;
        }
        handleUserReply(reply);
    });
}

void ScrapeController::fetchNextPage(const QString &paginationToken)
{
    m_currentPaginationToken = paginationToken;
    setStatus(QStringLiteral("Fetched %1 posts").arg(m_posts.size()));

    QUrl url(QStringLiteral("%1/2/users/%2/tweets").arg(ApiBase, m_userId));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("max_results"), QStringLiteral("100"));
    query.addQueryItem(QStringLiteral("tweet.fields"),
                       QStringLiteral("article,attachments,author_id,card_uri,context_annotations,conversation_id,created_at,display_text_range,edit_controls,entities,geo,id,in_reply_to_user_id,lang,note_tweet,possibly_sensitive,public_metrics,referenced_tweets,reply_settings,scopes,source,text,withheld"));
    query.addQueryItem(QStringLiteral("expansions"),
                       QStringLiteral("article.cover_media,article.media_entities,attachments.media_keys,attachments.poll_ids,author_id,entities.mentions.username,geo.place_id,in_reply_to_user_id,referenced_tweets.id,referenced_tweets.id.attachments.media_keys,referenced_tweets.id.author_id"));
    query.addQueryItem(QStringLiteral("media.fields"),
                       QStringLiteral("alt_text,duration_ms,height,media_key,preview_image_url,public_metrics,type,url,variants,width"));
    query.addQueryItem(QStringLiteral("poll.fields"),
                       QStringLiteral("duration_minutes,end_datetime,id,options,voting_status"));
    query.addQueryItem(QStringLiteral("place.fields"),
                       QStringLiteral("contained_within,country,country_code,full_name,geo,id,name,place_type"));
    query.addQueryItem(QStringLiteral("user.fields"),
                       QStringLiteral("created_at,description,entities,id,location,name,profile_image_url,protected,public_metrics,url,username,verified,verified_type"));

    QStringList excludes;
    if (m_excludeReplies)
        excludes.append(QStringLiteral("replies"));
    if (m_excludeReposts)
        excludes.append(QStringLiteral("retweets"));
    if (!excludes.isEmpty())
        query.addQueryItem(QStringLiteral("exclude"), excludes.join(u','));
    if (!paginationToken.isEmpty())
        query.addQueryItem(QStringLiteral("pagination_token"), paginationToken);

    url.setQuery(query);
    auto *reply = m_network.get(request(url));
    m_activeReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (m_activeReply == reply)
            m_activeReply = nullptr;
        if (!m_running) {
            reply->deleteLater();
            return;
        }
        handleTimelineReply(reply);
    });
}

void ScrapeController::handleUserReply(QNetworkReply *reply)
{
    const auto guard = qScopeGuard([reply] { reply->deleteLater(); });
    if (httpStatus(reply) == 429) {
        retryAfterRateLimit(QStringLiteral("user lookup"), retryDelayMs(reply), [this] { lookupUser(); });
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        const auto body = QString::fromUtf8(reply->readAll()).trimmed();
        finishWithError(body.isEmpty() ? reply->errorString() : QStringLiteral("%1: %2").arg(reply->errorString(), body));
        return;
    }

    const auto document = QJsonDocument::fromJson(reply->readAll());
    const auto root = document.object();
    m_user = objectAt(root, QStringLiteral("data"));
    m_userId = m_user.value(QStringLiteral("id")).toString();
    if (m_userId.isEmpty()) {
        finishWithError(QStringLiteral("Could not resolve that username."));
        return;
    }

    fetchNextPage();
}

void ScrapeController::scheduleNextPage(const QString &paginationToken)
{
    setStatus(QStringLiteral("Fetched %1 posts. Waiting 1 second before next API call.").arg(m_posts.size()));
    QTimer::singleShot(TimelinePageDelayMs, this, [this, paginationToken] {
        if (!m_running)
            return;
        fetchNextPage(paginationToken);
    });
}

void ScrapeController::handleTimelineReply(QNetworkReply *reply)
{
    const auto guard = qScopeGuard([reply] { reply->deleteLater(); });
    if (httpStatus(reply) == 429) {
        const auto token = m_currentPaginationToken;
        retryAfterRateLimit(QStringLiteral("timeline page"), retryDelayMs(reply), [this, token] { fetchNextPage(token); });
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        const auto body = QString::fromUtf8(reply->readAll()).trimmed();
        finishWithError(body.isEmpty() ? reply->errorString() : QStringLiteral("%1: %2").arg(reply->errorString(), body));
        return;
    }

    const auto document = QJsonDocument::fromJson(reply->readAll());
    const auto root = document.object();
    m_rawPages.append(root);

    const auto includes = objectAt(root, QStringLiteral("includes"));
    mergeById(m_mediaByKey, arrayAt(includes, QStringLiteral("media")), QStringLiteral("media_key"));
    mergeById(m_usersById, arrayAt(includes, QStringLiteral("users")), QStringLiteral("id"));
    mergeById(m_placesById, arrayAt(includes, QStringLiteral("places")), QStringLiteral("id"));
    mergeById(m_pollsById, arrayAt(includes, QStringLiteral("polls")), QStringLiteral("id"));

    for (const auto &value : arrayAt(root, QStringLiteral("data"))) {
        if (!value.isObject())
            continue;
        auto post = value.toObject();
        post.insert(QStringLiteral("linked_content"), linkedContentFromPost(post));
        m_posts.append(post);
    }
    emit postsCountChanged();

    const auto meta = objectAt(root, QStringLiteral("meta"));
    const auto nextToken = meta.value(QStringLiteral("next_token")).toString();
    if (!nextToken.isEmpty()) {
        scheduleNextPage(nextToken);
        return;
    }

    finishOk();
}

void ScrapeController::retryAfterRateLimit(const QString &what, int delayMs, const std::function<void()> &retry)
{
    setStatus(QStringLiteral("Rate limited during %1 after %2 posts. Waiting %3 before retrying.")
                  .arg(what)
                  .arg(m_posts.size())
                  .arg(humanDelay(delayMs)));

    QTimer::singleShot(delayMs, this, [this, retry] {
        if (!m_running)
            return;
        setStatus(QStringLiteral("Retrying after rate limit; %1 posts fetched").arg(m_posts.size()));
        retry();
    });
}

bool ScrapeController::importArchiveFiles(const QString &archiveDirectory)
{
    QDir root(archiveDirectory);
    if (!root.exists()) {
        finishWithError(QStringLiteral("Archive folder does not exist."));
        return false;
    }

    const auto dataPath = root.exists(QStringLiteral("data")) ? root.filePath(QStringLiteral("data")) : root.path();
    QDir dataDir(dataPath);
    const auto jsFiles = dataDir.entryInfoList(QStringList { QStringLiteral("*.js") },
                                               QDir::Files, QDir::Name);

    if (jsFiles.isEmpty()) {
        finishWithError(QStringLiteral("No archive JavaScript files found. Select the extracted archive folder."));
        return false;
    }

    for (const auto &info : jsFiles) {
        QString error;
        const auto document = archiveJsDocument(info.filePath(), &error);
        if (document.isNull())
            continue;

        const auto array = document.array();
        if (info.fileName().compare(QStringLiteral("account.js"), Qt::CaseInsensitive) == 0) {
            for (const auto &value : array) {
                const auto account = objectAt(value.toObject(), QStringLiteral("account"));
                if (account.isEmpty())
                    continue;
                m_user.insert(QStringLiteral("archive_account"), account);
                m_username = account.value(QStringLiteral("username")).toString();
                break;
            }
            continue;
        }

        if (!fileNameLooksTweetArchive(info.fileName()))
            continue;

        QJsonArray sourceRecords;
        int importedFromFile = 0;
        for (const auto &value : array) {
            const auto wrapper = value.toObject();
            const auto tweet = objectAt(wrapper, QStringLiteral("tweet"));
            if (tweet.isEmpty())
                continue;

            auto post = normalizedArchivePost(tweet);
            if (m_excludeReplies && isReplyPost(post))
                continue;
            if (m_excludeReposts && isRepostPost(post))
                continue;

            post.insert(QStringLiteral("linked_content"), linkedContentFromPost(post));
            post.insert(QStringLiteral("embedded_media"), archiveMediaFromPost(tweet));
            post.insert(QStringLiteral("raw_archive_tweet"), tweet);
            m_posts.append(post);
            sourceRecords.append(wrapper);
            ++importedFromFile;
        }

        if (importedFromFile > 0) {
            QJsonObject rawPage;
            rawPage.insert(QStringLiteral("file"), info.fileName());
            rawPage.insert(QStringLiteral("records"), sourceRecords);
            m_rawPages.append(rawPage);
            emit postsCountChanged();
            setStatus(QStringLiteral("Imported %1 posts").arg(m_posts.size()));
        }
    }

    if (m_posts.isEmpty()) {
        finishWithError(QStringLiteral("No tweet records found in that archive folder."));
        return false;
    }

    if (m_username.isEmpty()) {
        const auto first = m_posts.first().toObject();
        m_username = first.value(QStringLiteral("author_id")).toString();
    }
    if (m_username.isEmpty())
        m_username = QStringLiteral("x_archive");

    return true;
}

void ScrapeController::prepareExportProcessing(const QJsonObject &exportRoot)
{
    QDir dir(m_outputDirectory);
    if (!dir.exists())
        dir.mkpath(QStringLiteral("."));
    dir.mkpath(QStringLiteral("media"));

    const auto includes = objectAt(exportRoot, QStringLiteral("includes"));
    QJsonObject mediaByKey;
    for (const auto &value : arrayAt(includes, QStringLiteral("media"))) {
        const auto media = value.toObject();
        const auto key = media.value(QStringLiteral("media_key")).toString();
        if (!key.isEmpty())
            mediaByKey.insert(key, media);
    }

    QSet<QString> seenUrls;
    for (const auto &value : m_posts) {
        const auto post = value.toObject();
        const auto postId = post.value(QStringLiteral("id")).toString(QStringLiteral("post"));
        int index = 1;
        for (const auto &mediaValue : mediaItemsForPost(post, mediaByKey)) {
            const auto media = mediaValue.toObject();
            const auto url = bestMediaUrl(media);
            if (!url.isValid() || seenUrls.contains(url.toString()))
                continue;
            seenUrls.insert(url.toString());

            const auto type = media.value(QStringLiteral("type")).toString();
            const auto fallback = type == QStringLiteral("video") || type == QStringLiteral("animated_gif")
                ? QStringLiteral("mp4")
                : QStringLiteral("jpg");
            const auto ext = extensionForUrl(url, fallback);
            const auto base = QStringLiteral("%1_%2.%3")
                                  .arg(sanitizeFilePart(postId))
                                  .arg(index++, 2, 10, QLatin1Char('0'))
                                  .arg(ext);
            m_pendingDownloads.push_back({ url, dir.filePath(QStringLiteral("media/%1").arg(base)), postId });
        }
    }

    m_totalDownloads = m_pendingDownloads.size();
    emit mediaProgressChanged();
}

bool ScrapeController::writePostsText(const QJsonObject &exportRoot)
{
    QDir dir(m_outputDirectory);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        finishWithError(QStringLiteral("Could not create output directory."));
        return false;
    }

    const auto path = dir.filePath(QStringLiteral("posts.txt"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        finishWithError(QStringLiteral("Could not write posts.txt."));
        return false;
    }

    const auto username = exportUsername(exportRoot);
    const auto includes = objectAt(exportRoot, QStringLiteral("includes"));
    QJsonObject mediaByKey;
    for (const auto &value : arrayAt(includes, QStringLiteral("media"))) {
        const auto media = value.toObject();
        const auto key = media.value(QStringLiteral("media_key")).toString();
        if (!key.isEmpty())
            mediaByKey.insert(key, media);
    }

    QTextStream out(&file);
    out << "Xscraper posts export\n";
    if (!username.isEmpty())
        out << "User: @" << username << "\n";
    out << "Posts: " << m_posts.size() << "\n";
    out << "Generated: " << QDateTime::currentDateTimeUtc().toString(Qt::ISODate) << "\n";
    out << QString(72, '=') << "\n\n";

    int number = 1;
    for (const auto &value : m_posts) {
        const auto post = value.toObject();
        const auto id = post.value(QStringLiteral("id")).toString();
        out << "[" << number++ << "] " << post.value(QStringLiteral("created_at")).toString() << "\n";
        if (!id.isEmpty())
            out << "ID: " << id << "\n";
        const auto url = postUrl(post, username);
        if (!url.isEmpty())
            out << "URL: " << url << "\n";

        const auto text = displayText(post);
        if (!text.isEmpty())
            out << "\n" << text << "\n";

        const auto links = post.value(QStringLiteral("linked_content")).toArray();
        if (!links.isEmpty()) {
            out << "\nLinks:\n";
            for (const auto &linkValue : links) {
                const auto link = linkValue.toObject();
                const auto expanded = link.value(QStringLiteral("expanded_url")).toString(
                    link.value(QStringLiteral("unwound_url")).toString(link.value(QStringLiteral("url")).toString()));
                if (!expanded.isEmpty())
                    out << "- " << expanded << "\n";
            }
        }

        const auto mediaItems = mediaItemsForPost(post, mediaByKey);
        if (!mediaItems.isEmpty()) {
            out << "\nMedia:\n";
            int mediaIndex = 1;
            for (const auto &mediaValue : mediaItems) {
                const auto media = mediaValue.toObject();
                const auto mediaUrl = bestMediaUrl(media).toString();
                const auto type = media.value(QStringLiteral("type")).toString(QStringLiteral("media"));
                out << "- " << type;
                if (!mediaUrl.isEmpty())
                    out << ": " << mediaUrl;
                const auto idPart = id.isEmpty() ? QStringLiteral("post") : id;
                const auto fallback = type == QStringLiteral("video") || type == QStringLiteral("animated_gif")
                    ? QStringLiteral("mp4")
                    : QStringLiteral("jpg");
                const auto ext = extensionForUrl(QUrl(mediaUrl), fallback);
                out << " -> media/" << sanitizeFilePart(idPart) << "_"
                    << QStringLiteral("%1").arg(mediaIndex++, 2, 10, QLatin1Char('0')) << "." << ext << "\n";
            }
        }

        out << "\n" << QString(72, '-') << "\n\n";
    }

    setOutputPath(path);
    return true;
}

bool ScrapeController::writePostsHtml(const QJsonObject &exportRoot)
{
    QDir dir(m_outputDirectory);
    const auto path = dir.filePath(QStringLiteral("posts.html"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        finishWithError(QStringLiteral("Could not write posts.html."));
        return false;
    }

    // Map each post id to the media files the downloader is about to fetch, so
    // the HTML references the exact local paths that will exist on disk.
    QHash<QString, QVector<QPair<QString, bool>>> mediaByPost;
    for (const auto &download : m_pendingDownloads) {
        const auto rel = dir.relativeFilePath(download.path);
        const auto ext = QFileInfo(download.path).suffix().toLower();
        const bool isVideo = ext == QStringLiteral("mp4") || ext == QStringLiteral("m4v")
            || ext == QStringLiteral("mov") || ext == QStringLiteral("webm");
        mediaByPost[download.postId].append({ rel, isVideo });
    }

    const auto handle = exportUsername(exportRoot);
    const auto user = objectAt(exportRoot, QStringLiteral("user"));
    auto name = user.value(QStringLiteral("name")).toString();
    if (name.isEmpty())
        name = objectAt(user, QStringLiteral("archive_account")).value(QStringLiteral("accountDisplayName")).toString();
    if (name.isEmpty())
        name = handle.isEmpty() ? QStringLiteral("X export") : handle;
    auto avatar = user.value(QStringLiteral("profile_image_url")).toString();
    avatar.replace(QStringLiteral("_normal"), QStringLiteral("_400x400"));

    const auto nameEsc = name.toHtmlEscaped();
    const auto handleEsc = handle.toHtmlEscaped();
    const auto avatarEsc = avatar.toHtmlEscaped();

    QTextStream out(&file);
    out << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n";
    out << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
    out << "<title>" << (handle.isEmpty() ? nameEsc : QStringLiteral("@%1").arg(handleEsc)) << " \xC2\xB7 Xscraper</title>\n";
    out << R"CSS(<style>
:root{--bg:#000;--card:#16181c;--border:#2f3336;--text:#e7e9ea;--muted:#71767b;--accent:#1d9bf0;}
*{box-sizing:border-box;}
body{margin:0;background:var(--bg);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;line-height:1.45;}
a{color:var(--accent);text-decoration:none;}
a:hover{text-decoration:underline;}
.wrap{max-width:600px;margin:0 auto;border-left:1px solid var(--border);border-right:1px solid var(--border);min-height:100vh;}
.profile{display:flex;gap:12px;align-items:center;padding:14px 16px;border-bottom:1px solid var(--border);position:sticky;top:0;background:rgba(0,0,0,.85);backdrop-filter:blur(10px);z-index:1;}
.avatar{width:48px;height:48px;border-radius:50%;object-fit:cover;background:var(--border);flex:none;}
.pname{font-weight:800;font-size:20px;line-height:1.2;}
.phandle{color:var(--muted);font-size:14px;}
.profile .meta{margin-left:auto;color:var(--muted);font-size:13px;}
.tweet{display:flex;gap:12px;padding:12px 16px;border-bottom:1px solid var(--border);}
.tweet:hover{background:#080808;}
.tweet .avatar{width:40px;height:40px;}
.body{flex:1;min-width:0;}
.head{display:flex;flex-wrap:wrap;align-items:baseline;gap:4px;font-size:15px;}
.head .name{font-weight:700;}
.head .handle,.head .time{color:var(--muted);}
.head .time{margin-left:auto;white-space:nowrap;}
.text{margin-top:2px;font-size:15px;white-space:pre-wrap;overflow-wrap:anywhere;}
.media{margin-top:10px;display:grid;gap:3px;border-radius:16px;overflow:hidden;border:1px solid var(--border);}
.media.n1{grid-template-columns:1fr;}
.media.n2,.media.n3,.media.n4{grid-template-columns:1fr 1fr;}
.media img,.media video{width:100%;height:100%;max-height:520px;object-fit:cover;display:block;background:#000;cursor:pointer;}
.media.n1 img,.media.n1 video{max-height:600px;object-fit:contain;}
.actions{display:flex;gap:28px;margin-top:10px;color:var(--muted);font-size:13px;}
.actions a{color:var(--muted);}
.empty,.footer{padding:32px 16px;text-align:center;color:var(--muted);font-size:13px;}
</style>
)CSS";
    out << "</head>\n<body>\n<main class=\"wrap\">\n";

    out << "<header class=\"profile\">\n";
    if (!avatar.isEmpty())
        out << "<img class=\"avatar\" src=\"" << avatarEsc << "\" alt=\"\" onerror=\"this.style.visibility='hidden'\">\n";
    out << "<div><div class=\"pname\">" << nameEsc << "</div>";
    if (!handle.isEmpty())
        out << "<div class=\"phandle\"><a href=\"https://x.com/" << handleEsc << "\" target=\"_blank\" rel=\"noopener\">@" << handleEsc << "</a></div>";
    out << "</div>\n<div class=\"meta\">" << m_posts.size() << " posts</div>\n</header>\n";

    if (m_posts.isEmpty())
        out << "<div class=\"empty\">No posts.</div>\n";

    for (const auto &value : m_posts) {
        const auto post = value.toObject();
        const auto id = post.value(QStringLiteral("id")).toString();
        const auto url = postUrl(post, handle);
        const auto urlEsc = url.toHtmlEscaped();
        const auto time = formatTimestamp(post.value(QStringLiteral("created_at")).toString());

        out << "<article class=\"tweet\">\n";
        if (!avatar.isEmpty())
            out << "<img class=\"avatar\" src=\"" << avatarEsc << "\" alt=\"\" onerror=\"this.style.visibility='hidden'\">\n";
        out << "<div class=\"body\">\n<div class=\"head\"><span class=\"name\">" << nameEsc << "</span>";
        if (!handle.isEmpty())
            out << "<span class=\"handle\">@" << handleEsc << "</span>";
        if (!time.isEmpty()) {
            if (!url.isEmpty())
                out << "<a class=\"time\" href=\"" << urlEsc << "\" target=\"_blank\" rel=\"noopener\">" << time.toHtmlEscaped() << "</a>";
            else
                out << "<span class=\"time\">" << time.toHtmlEscaped() << "</span>";
        }
        out << "</div>\n";

        const auto body = linkifyText(post);
        if (!body.isEmpty())
            out << "<div class=\"text\">" << body << "</div>\n";

        const auto media = mediaByPost.value(id);
        if (!media.isEmpty()) {
            out << "<div class=\"media n" << qMin(4, media.size()) << "\">\n";
            for (const auto &item : media) {
                const auto rel = item.first.toHtmlEscaped();
                if (item.second)
                    out << "<video controls preload=\"metadata\" src=\"" << rel << "\" onerror=\"this.style.display='none'\"></video>\n";
                else
                    out << "<a href=\"" << rel << "\" target=\"_blank\" rel=\"noopener\"><img loading=\"lazy\" src=\"" << rel << "\" alt=\"\" onerror=\"this.parentElement.style.display='none'\"></a>\n";
            }
            out << "</div>\n";
        }

        const auto metrics = objectAt(post, QStringLiteral("public_metrics"));
        const int replies = metrics.value(QStringLiteral("reply_count")).toInt();
        const int reposts = metrics.contains(QStringLiteral("retweet_count"))
            ? metrics.value(QStringLiteral("retweet_count")).toInt()
            : post.value(QStringLiteral("retweet_count")).toInt();
        const int likes = metrics.contains(QStringLiteral("like_count"))
            ? metrics.value(QStringLiteral("like_count")).toInt()
            : post.value(QStringLiteral("favorite_count")).toInt();
        out << "<div class=\"actions\">";
        out << "<span>" << QStringLiteral(u"↩ ") << replies << "</span>";
        out << "<span>" << QStringLiteral(u"⇄ ") << reposts << "</span>";
        out << "<span>" << QStringLiteral(u"♥ ") << likes << "</span>";
        if (!url.isEmpty())
            out << "<a href=\"" << urlEsc << "\" target=\"_blank\" rel=\"noopener\">Open on X</a>";
        out << "</div>\n</div>\n</article>\n";
    }

    out << "<footer class=\"footer\">Generated by Xscraper \xC2\xB7 "
        << QDateTime::currentDateTimeUtc().toString(Qt::ISODate) << "</footer>\n";
    out << "</main>\n</body>\n</html>\n";

    setOutputPath(path);
    return true;
}

void ScrapeController::downloadNextMedia()
{
    if (!m_running)
        return;

    if (m_pendingDownloads.isEmpty()) {
        m_currentMedia.clear();
        emit mediaProgressChanged();
        setStatus(QStringLiteral("Wrote posts.txt, posts.html, and downloaded %1/%2 media files%3")
                      .arg(m_completedDownloads)
                      .arg(m_totalDownloads)
                      .arg(m_failedDownloads > 0 ? QStringLiteral(" (%1 failed)").arg(m_failedDownloads) : QString()));
        setRunning(false);
        emit finished(m_failedDownloads == 0);
        return;
    }

    const auto item = m_pendingDownloads.takeFirst();
    QFileInfo info(item.path);
    QDir().mkpath(info.absolutePath());
    if (info.exists() && info.size() > 0) {
        ++m_completedDownloads;
        m_currentMedia = QStringLiteral("Skipped existing %1").arg(info.fileName());
        emit mediaProgressChanged();
        downloadNextMedia();
        return;
    }

    setStatus(QStringLiteral("Downloading media %1/%2").arg(m_completedDownloads + m_failedDownloads + 1).arg(m_totalDownloads));
    m_currentMedia = QStringLiteral("%1 -> %2").arg(item.url.toString(), item.path);
    emit mediaProgressChanged();
    QNetworkRequest req(item.url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("xscraper/0.1"));
    auto *reply = m_network.get(req);
    m_activeReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, path = item.path] {
        if (m_activeReply == reply)
            m_activeReply = nullptr;
        if (!m_running) {
            reply->deleteLater();
            return;
        }
        handleMediaReply(reply, path);
    });
}

void ScrapeController::handleMediaReply(QNetworkReply *reply, const QString &path)
{
    const auto guard = qScopeGuard([reply] { reply->deleteLater(); });
    if (reply->error() != QNetworkReply::NoError) {
        ++m_failedDownloads;
        m_currentMedia = QStringLiteral("Failed: %1").arg(reply->url().toString());
        emit mediaProgressChanged();
        downloadNextMedia();
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        ++m_failedDownloads;
        m_currentMedia = QStringLiteral("Could not write: %1").arg(path);
        emit mediaProgressChanged();
        downloadNextMedia();
        return;
    }

    file.write(reply->readAll());
    ++m_completedDownloads;
    m_currentMedia = QStringLiteral("Saved: %1").arg(path);
    emit mediaProgressChanged();
    downloadNextMedia();
}

void ScrapeController::finishOk()
{
    if (!writeExport())
        return;

    setStatus(QStringLiteral("Saved %1 posts").arg(m_posts.size()));
    setRunning(false);
    emit finished(true);
}

void ScrapeController::finishWithError(const QString &message)
{
    setError(message);
    setStatus(message);
    setRunning(false);
    emit finished(false);
}

bool ScrapeController::writeExport()
{
    QDir dir(m_outputDirectory);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        finishWithError(QStringLiteral("Could not create output directory."));
        return false;
    }

    const auto stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddTHHmmssZ"));
    const auto fileName = QStringLiteral("%1_%2.json").arg(m_username, stamp);
    const auto path = dir.filePath(fileName);

    QJsonObject includes;
    includes.insert(QStringLiteral("media"), valuesFromMap(m_mediaByKey));
    includes.insert(QStringLiteral("users"), valuesFromMap(m_usersById));
    includes.insert(QStringLiteral("places"), valuesFromMap(m_placesById));
    includes.insert(QStringLiteral("polls"), valuesFromMap(m_pollsById));

    QJsonObject exportRoot;
    exportRoot.insert(QStringLiteral("schema"), QStringLiteral("xscraper.export.v1"));
    exportRoot.insert(QStringLiteral("created_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    exportRoot.insert(QStringLiteral("source"), m_source);
    QJsonObject filters;
    filters.insert(QStringLiteral("exclude_replies"), m_excludeReplies);
    filters.insert(QStringLiteral("exclude_reposts"), m_excludeReposts);
    exportRoot.insert(QStringLiteral("filters"), filters);
    exportRoot.insert(QStringLiteral("user"), m_user);
    exportRoot.insert(QStringLiteral("post_count"), m_posts.size());
    exportRoot.insert(QStringLiteral("posts"), m_posts);
    exportRoot.insert(QStringLiteral("includes"), includes);
    exportRoot.insert(QStringLiteral("raw_pages"), m_rawPages);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        finishWithError(QStringLiteral("Could not write export file."));
        return false;
    }

    file.write(QJsonDocument(exportRoot).toJson(QJsonDocument::Indented));
    setOutputPath(path);
    return true;
}

QJsonArray ScrapeController::valuesFromMap(const QJsonObject &object)
{
    QJsonArray values;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        values.append(it.value());
    return values;
}

QJsonArray ScrapeController::linkedContentFromPost(const QJsonObject &post)
{
    QJsonArray links;
    const auto entities = objectAt(post, QStringLiteral("entities"));
    for (const auto &value : arrayAt(entities, QStringLiteral("urls"))) {
        if (!value.isObject())
            continue;
        const auto url = value.toObject();
        QJsonObject link;
        link.insert(QStringLiteral("url"), url.value(QStringLiteral("url")));
        link.insert(QStringLiteral("expanded_url"), url.value(QStringLiteral("expanded_url")));
        link.insert(QStringLiteral("display_url"), url.value(QStringLiteral("display_url")));
        link.insert(QStringLiteral("unwound_url"), url.value(QStringLiteral("unwound_url")));
        link.insert(QStringLiteral("title"), url.value(QStringLiteral("title")));
        link.insert(QStringLiteral("description"), url.value(QStringLiteral("description")));
        links.append(link);
    }
    return links;
}

QJsonDocument ScrapeController::archiveJsDocument(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Could not open %1").arg(path);
        return {};
    }

    auto bytes = file.readAll().trimmed();
    const auto equals = bytes.indexOf('=');
    if (equals >= 0)
        bytes = bytes.mid(equals + 1).trimmed();
    if (bytes.endsWith(';'))
        bytes.chop(1);

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error)
            *error = QStringLiteral("%1: %2").arg(path, parseError.errorString());
        return {};
    }
    return document;
}

QJsonObject ScrapeController::normalizedArchivePost(const QJsonObject &tweet)
{
    QJsonObject post;
    post.insert(QStringLiteral("id"), tweet.value(QStringLiteral("id_str")).toString(tweet.value(QStringLiteral("id")).toString()));
    post.insert(QStringLiteral("author_id"), tweet.value(QStringLiteral("user_id_str")).toString(tweet.value(QStringLiteral("user_id")).toString()));
    post.insert(QStringLiteral("created_at"), tweet.value(QStringLiteral("created_at")));
    post.insert(QStringLiteral("text"), tweet.value(QStringLiteral("full_text")).toString(tweet.value(QStringLiteral("text")).toString()));
    post.insert(QStringLiteral("source"), tweet.value(QStringLiteral("source")));
    post.insert(QStringLiteral("lang"), tweet.value(QStringLiteral("lang")));
    post.insert(QStringLiteral("entities"), tweet.value(QStringLiteral("entities")));
    post.insert(QStringLiteral("extended_entities"), tweet.value(QStringLiteral("extended_entities")));
    post.insert(QStringLiteral("in_reply_to_status_id"), tweet.value(QStringLiteral("in_reply_to_status_id_str")).toString());
    post.insert(QStringLiteral("in_reply_to_user_id"), tweet.value(QStringLiteral("in_reply_to_user_id_str")).toString());
    post.insert(QStringLiteral("conversation_id"), tweet.value(QStringLiteral("conversation_id_str")).toString());
    post.insert(QStringLiteral("possibly_sensitive"), tweet.value(QStringLiteral("possibly_sensitive")));
    post.insert(QStringLiteral("favorite_count"), tweet.value(QStringLiteral("favorite_count")));
    post.insert(QStringLiteral("retweet_count"), tweet.value(QStringLiteral("retweet_count")));
    return post;
}

QJsonArray ScrapeController::archiveMediaFromPost(const QJsonObject &tweet)
{
    QJsonArray media;
    const auto extended = objectAt(tweet, QStringLiteral("extended_entities"));
    auto mediaItems = arrayAt(extended, QStringLiteral("media"));
    if (mediaItems.isEmpty())
        mediaItems = arrayAt(objectAt(tweet, QStringLiteral("entities")), QStringLiteral("media"));

    for (const auto &value : mediaItems) {
        if (!value.isObject())
            continue;
        const auto item = value.toObject();
        QJsonObject out;
        out.insert(QStringLiteral("id"), item.value(QStringLiteral("id_str")).toString(item.value(QStringLiteral("id")).toString()));
        out.insert(QStringLiteral("type"), item.value(QStringLiteral("type")));
        out.insert(QStringLiteral("url"), item.value(QStringLiteral("url")));
        out.insert(QStringLiteral("expanded_url"), item.value(QStringLiteral("expanded_url")));
        out.insert(QStringLiteral("media_url"), item.value(QStringLiteral("media_url_https")).toString(item.value(QStringLiteral("media_url")).toString()));
        out.insert(QStringLiteral("sizes"), item.value(QStringLiteral("sizes")));
        out.insert(QStringLiteral("video_info"), item.value(QStringLiteral("video_info")));
        media.append(out);
    }
    return media;
}

bool ScrapeController::isReplyPost(const QJsonObject &post)
{
    return !post.value(QStringLiteral("in_reply_to_status_id")).toString().isEmpty()
        || !post.value(QStringLiteral("in_reply_to_user_id")).toString().isEmpty();
}

bool ScrapeController::isRepostPost(const QJsonObject &post)
{
    return post.value(QStringLiteral("text")).toString().startsWith(QStringLiteral("RT @"));
}

QString ScrapeController::exportUsername(const QJsonObject &exportRoot)
{
    const auto user = objectAt(exportRoot, QStringLiteral("user"));
    auto username = user.value(QStringLiteral("username")).toString();
    if (username.isEmpty())
        username = objectAt(user, QStringLiteral("archive_account")).value(QStringLiteral("username")).toString();
    return username;
}

QString ScrapeController::postUrl(const QJsonObject &post, const QString &username)
{
    const auto id = post.value(QStringLiteral("id")).toString();
    if (id.isEmpty() || username.isEmpty())
        return {};
    return QStringLiteral("https://x.com/%1/status/%2").arg(username, id);
}

QString ScrapeController::displayText(const QJsonObject &post)
{
    auto text = post.value(QStringLiteral("text")).toString();
    const auto noteTweet = objectAt(post, QStringLiteral("note_tweet"));
    const auto noteText = noteTweet.value(QStringLiteral("text")).toString();
    if (!noteText.isEmpty())
        text = noteText;
    return text;
}

QString ScrapeController::linkifyText(const QJsonObject &post)
{
    QString text = displayText(post).trimmed().toHtmlEscaped();
    if (text.isEmpty())
        return text;

    static const QRegularExpression mention(QStringLiteral("(?<![\\w@/])@(\\w+)"));
    text.replace(mention, QStringLiteral("<a href=\"https://x.com/\\1\" target=\"_blank\" rel=\"noopener\">@\\1</a>"));

    static const QRegularExpression hashtag(QStringLiteral("(?<!\\w)([#＃])(\\w+)"));
    text.replace(hashtag, QStringLiteral("<a href=\"https://x.com/hashtag/\\2\" target=\"_blank\" rel=\"noopener\">\\1\\2</a>"));

    // Replace each t.co short link with an anchor to the expanded destination.
    QJsonArray urls = arrayAt(objectAt(post, QStringLiteral("entities")), QStringLiteral("urls"));
    for (const auto &value : arrayAt(objectAt(objectAt(post, QStringLiteral("note_tweet")), QStringLiteral("entities")), QStringLiteral("urls")))
        urls.append(value);

    for (const auto &value : urls) {
        const auto link = value.toObject();
        const auto shortUrl = link.value(QStringLiteral("url")).toString();
        if (shortUrl.isEmpty())
            continue;
        const auto expanded = link.value(QStringLiteral("expanded_url")).toString(
            link.value(QStringLiteral("unwound_url")).toString(shortUrl));
        auto display = link.value(QStringLiteral("display_url")).toString();
        if (display.isEmpty())
            display = expanded;
        const auto anchor = QStringLiteral("<a href=\"%1\" target=\"_blank\" rel=\"noopener\">%2</a>")
                                .arg(expanded.toHtmlEscaped(), display.toHtmlEscaped());
        text.replace(shortUrl.toHtmlEscaped(), anchor);
    }

    return text;
}

QString ScrapeController::formatTimestamp(const QString &raw)
{
    if (raw.isEmpty())
        return {};

    QDateTime dt = QDateTime::fromString(raw, Qt::ISODateWithMs);
    if (!dt.isValid())
        dt = QDateTime::fromString(raw, Qt::ISODate);
    if (!dt.isValid())
        dt = QLocale(QLocale::English).toDateTime(raw, QStringLiteral("ddd MMM dd HH:mm:ss +0000 yyyy"));
    if (!dt.isValid())
        return raw;

    dt.setTimeSpec(Qt::UTC);
    return dt.toString(QStringLiteral("MMM d, yyyy"));
}

QString ScrapeController::sanitizeFilePart(QString text)
{
    text = text.trimmed();
    if (text.isEmpty())
        text = QStringLiteral("post");
    text.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("_"));
    text = text.left(120);
    while (text.startsWith(u'.'))
        text.remove(0, 1);
    return text.isEmpty() ? QStringLiteral("post") : text;
}

QString ScrapeController::extensionForUrl(const QUrl &url, const QString &fallback)
{
    const auto path = url.path();
    const auto suffix = QFileInfo(path).suffix().toLower();
    if (!suffix.isEmpty() && suffix.size() <= 5)
        return suffix;
    return fallback;
}

QJsonArray ScrapeController::mediaItemsForPost(const QJsonObject &post, const QJsonObject &mediaByKey)
{
    QJsonArray mediaItems = post.value(QStringLiteral("embedded_media")).toArray();
    const auto attachments = objectAt(post, QStringLiteral("attachments"));
    for (const auto &keyValue : arrayAt(attachments, QStringLiteral("media_keys"))) {
        const auto key = keyValue.toString();
        const auto media = mediaByKey.value(key);
        if (media.isObject())
            mediaItems.append(media);
    }
    return mediaItems;
}

QUrl ScrapeController::bestMediaUrl(const QJsonObject &media)
{
    const auto direct = media.value(QStringLiteral("media_url")).toString(
        media.value(QStringLiteral("url")).toString());
    const auto type = media.value(QStringLiteral("type")).toString();
    if ((type == QStringLiteral("photo") || type.isEmpty()) && !direct.isEmpty())
        return QUrl(direct);

    int bestBitrate = -1;
    QString bestUrl;
    const auto videoInfo = objectAt(media, QStringLiteral("video_info"));
    for (const auto &variantValue : arrayAt(videoInfo, QStringLiteral("variants"))) {
        const auto variant = variantValue.toObject();
        const auto url = variant.value(QStringLiteral("url")).toString();
        const auto contentType = variant.value(QStringLiteral("content_type")).toString();
        if (url.isEmpty() || (!contentType.isEmpty() && contentType != QStringLiteral("video/mp4")))
            continue;
        const auto bitrate = variant.value(QStringLiteral("bitrate")).toInt();
        if (bitrate >= bestBitrate) {
            bestBitrate = bitrate;
            bestUrl = url;
        }
    }
    if (!bestUrl.isEmpty())
        return QUrl(bestUrl);

    const auto preview = media.value(QStringLiteral("preview_image_url")).toString();
    if (!preview.isEmpty())
        return QUrl(preview);
    return QUrl(direct);
}
