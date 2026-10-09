/*
StreamMulticast — OBS Multi-RTMP Plugin with Per-Output Re-Encode
Copyright (C) 2026 Avanatro <contact@avanatro.com>

GPLv2 — see LICENSE for full text.
*/

#pragma once

#include "../core/Endpoint.hpp"
#include "../core/EndpointRegistry.hpp"
#include "../pipeline/EncoderFactory.hpp"

#include <QtWidgets/QDialog>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QLabel>
#include <QtWidgets/QScrollArea>

class QShowEvent;

namespace smulti {

class EndpointDialog : public QDialog {
	Q_OBJECT

public:
	explicit EndpointDialog(const Endpoint &ep,
	                        EndpointRegistry &registry,
	                        QWidget *parent = nullptr);

	const Endpoint &result_endpoint() const { return m_result; }

protected:
	void showEvent(QShowEvent *event) override;

private slots:
	void on_template_selected(int index);
	void on_show_key_toggled(bool visible);
	void on_test_connection();
	void on_import_from_obs();
	void on_import_tiktok_bridge();
	void on_video_mode_changed(int index);
	void on_audio_mode_changed(int index);
	void on_codec_changed(int index);
	void on_save();

private:
	void setup_ui();
	void populate_from_endpoint();
	Endpoint collect_from_form() const;
	void refresh_backend_choices(EncoderBackend preferred);
	void refresh_mode_controls();

	Endpoint m_endpoint;
	Endpoint m_result;
	EndpointRegistry &m_registry;

	/* Connection */
	QLineEdit *m_name_edit {nullptr};
	QComboBox *m_template_cb {nullptr};
	QLineEdit *m_server_edit {nullptr};
	QLineEdit *m_key_edit {nullptr};
	QPushButton *m_show_key_btn {nullptr};
	QPushButton *m_import_btn {nullptr};
	QPushButton *m_tiktok_bridge_btn {nullptr};

	/* Video */
	QComboBox *m_video_mode_cb {nullptr};
	QComboBox *m_codec_cb {nullptr};
	QComboBox *m_backend_cb {nullptr};
	QSpinBox *m_bitrate_spin {nullptr};
	QSpinBox *m_keyint_spin {nullptr};
	QComboBox *m_orientation_cb {nullptr};
	QLabel *m_video_hint {nullptr};

	/* Audio */
	QComboBox *m_audio_mode_cb {nullptr};
	QComboBox *m_audio_cb {nullptr};

	/* Behaviour */
	QCheckBox *m_linked_cb {nullptr};

	/* Footer */
	QPushButton *m_test_btn {nullptr};
	QPushButton *m_save_btn {nullptr};
	QPushButton *m_cancel_btn {nullptr};
	QLabel *m_status_label {nullptr};
	QScrollArea *m_scroll_area {nullptr};
	bool m_initial_geometry_applied {false};

	struct ServerTemplate {
		QString label;
		QString url;
	};
	static const std::vector<ServerTemplate> s_templates;
};

} // namespace smulti
