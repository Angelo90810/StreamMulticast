/*
StreamMulticast — Facebook Graph API integration for Broadcast Hub
Copyright (C) 2026 Avanatro <contact@avanatro.com>
GPLv2 — see LICENSE for full text.
*/

#pragma once

#include "../core/HubConfig.hpp"

#include <QObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QTimer>

namespace smulti {

class FacebookClient : public QObject {
	Q_OBJECT

public:
	explicit FacebookClient(QObject *parent = nullptr);

	void start_device_login(const QString &app_id, const QString &client_token);
	void fetch_pages(const QString &user_token);
	void create_live(const QString &page_id,
	                 const QString &page_token,
	                 const BroadcastPlan &plan);
	void publish_live(const QString &live_video_id,
	                  const QString &page_token);

signals:
	void device_code_ready(const QString &verification_uri,
	                       const QString &user_code);
	void authenticated(const QString &user_token);
	void pages_ready(const QJsonArray &pages);
	void live_created(const QString &live_video_id,
	                  const QString &secure_stream_url);
	void live_published();
	void error(const QString &message);

private:
	void poll_device_login();
	void emit_graph_error(const QByteArray &payload, const QString &fallback);
	QNetworkReply *post_form(const QUrl &url, const QUrlQuery &form);

	QNetworkAccessManager m_network;
	QTimer m_poll_timer;
	QString m_client_access_token;
	QString m_device_code;
};

} // namespace smulti
