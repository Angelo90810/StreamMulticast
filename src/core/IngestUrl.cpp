/*
StreamMulticast — RTMP ingest URL helpers
Copyright (C) 2026 Avanatro <contact@avanatro.com>
GPLv2 — see LICENSE for full text.
*/

#include "IngestUrl.hpp"

#include <QUrl>

namespace smulti {

SplitIngestUrl split_rtmp_ingest_url(const std::string &full_url)
{
	SplitIngestUrl result;
	QUrl parsed = QUrl::fromEncoded(QByteArray::fromStdString(full_url), QUrl::StrictMode);

	const QString scheme = parsed.scheme().toLower();
	if (!parsed.isValid() || parsed.host().isEmpty() ||
	    (scheme != QStringLiteral("rtmp") && scheme != QStringLiteral("rtmps")))
		return result;

	QString path = parsed.path(QUrl::FullyEncoded);
	const int slash = path.lastIndexOf('/');
	if (slash < 0 || slash == path.size() - 1)
		return result;

	QString key = path.mid(slash + 1);
	const QString query = parsed.query(QUrl::FullyEncoded);
	if (!query.isEmpty())
		key += QStringLiteral("?") + query;

	path = path.left(slash + 1);
	parsed.setPath(path, QUrl::StrictMode);
	parsed.setQuery(QString());
	parsed.setFragment(QString());

	result.server_url = parsed.toEncoded(QUrl::FullyEncoded).toStdString();
	result.stream_key = key.toStdString();
	result.ok = !result.server_url.empty() && !result.stream_key.empty();
	return result;
}

} // namespace smulti
