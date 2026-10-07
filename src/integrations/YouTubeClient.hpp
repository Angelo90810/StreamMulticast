/*
StreamMulticast — YouTube Live control-plane integration
Copyright (C) 2026 Avanatro <contact@avanatro.com>
GPLv2 — see LICENSE for full text.
*/

#pragma once

#include "../core/HubConfig.hpp"

#include <QObject>
#include <QNetworkAccessManager>
#include <QTcpServer>
#include <QUrl>

namespace smulti {

class YouTubeClient : public QObject {
	Q_OBJECT

public:
	explicit YouTubeClient(QObject *parent = nullptr);

	void start_oauth(const QString &client_id);
	void refresh_access_token(const QString &client_id,
	                          const QString &refresh_token);
	void fetch_channel(const QString &access_token);
	void prepare_native_obs_broadcast(const QString &access_token,
	                                  const QString &obs_stream_key,
	                                  const BroadcastPlan &plan);

signals:
	void authorization_url_ready(const QUrl &url);
	void authenticated(const QString &access_token,
	                   const QString &refresh_token);
	void access_token_refreshed(const QString &access_token);
	void channel_ready(const QString &channel_id,
	                   const QString &channel_name);
	void broadcast_prepared(const QString &broadcast_id,
	                        const QString &stream_id);
	void error(const QString &message);

private:
	QString random_urlsafe(int bytes) const;
	void accept_oauth_connection();
	void exchange_authorization_code(const QString &code);
	void create_broadcast(const QString &access_token,
	                      const QString &stream_id,
	                      const BroadcastPlan &plan);
	void bind_broadcast(const QString &access_token,
	                    const QString &broadcast_id,
	                    const QString &stream_id);
	void emit_google_error(const QByteArray &payload,
	                       const QString &fallback);

	QNetworkRequest authorized_request(const QUrl &url,
	                                   const QString &access_token) const;

	QNetworkAccessManager m_network;
	QTcpServer m_oauth_server;
	QString m_client_id;
	QString m_code_verifier;
	QString m_state;
	QString m_redirect_uri;
};

} // namespace smulti
