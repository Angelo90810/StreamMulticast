/*
StreamMulticast — OBS Multi-RTMP Plugin with Per-Output Re-Encode
Copyright (C) 2026 Avanatro <contact@avanatro.com>

GPLv2 — see LICENSE for full text.
*/

#include "EndpointDialog.hpp"
#include "../core/ObsServiceImport.hpp"
#include "../core/TikTokBridgeImport.hpp"

#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QSizePolicy>
#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>
#include <QtGui/QShowEvent>
#include <QtCore/QFileInfo>
#include <QtCore/QUrl>

#include <algorithm>

namespace smulti {

const std::vector<EndpointDialog::ServerTemplate> EndpointDialog::s_templates = {
	{ QStringLiteral("Custom RTMP"), QStringLiteral("") },
	{ QStringLiteral("Twitch"),      QStringLiteral("rtmp://live.twitch.tv/app") },
	{ QStringLiteral("YouTube"),     QStringLiteral("rtmps://a.rtmps.youtube.com:443/live2") },
	{ QStringLiteral("Facebook"),    QStringLiteral("rtmps://rtmp-api.facebook.com:443/rtmp/") },
	{ QStringLiteral("Instagram"),   QStringLiteral("rtmps://live-upload.instagram.com:443/rtmp/") },
	{ QStringLiteral("TikTok"),      QStringLiteral("rtmp://push-rtmp.tiktokcdn.com/live") },
	{ QStringLiteral("Trovo"),       QStringLiteral("rtmp://livepush.trovo.live/live/") },
};

EndpointDialog::EndpointDialog(const Endpoint &ep,
                               EndpointRegistry &registry,
                               QWidget *parent)
	: QDialog(parent), m_endpoint(ep), m_result(ep), m_registry(registry)
{
	setWindowTitle(tr("Endpoint Settings"));
	setModal(true);
	/* Keep a sensible desktop minimum, but do not force a 620px logical
	 * width on high-DPI/small work areas. showEvent() clamps the initial
	 * geometry to the actual monitor work area. */
	setMinimumWidth(520);
	setSizeGripEnabled(true);
	setup_ui();
	populate_from_endpoint();
}

void EndpointDialog::setup_ui()
{
	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(8, 8, 8, 8);
	outer->setSpacing(8);

	/* All variable-height form content lives inside a scroll area.  The
	 * action footer remains outside it, so Save/Cancel are always reachable
	 * even on 768p displays or Windows 125/150% scaling. */
	m_scroll_area = new QScrollArea(this);
	m_scroll_area->setWidgetResizable(true);
	m_scroll_area->setFrameShape(QFrame::NoFrame);
	m_scroll_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_scroll_area->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	m_scroll_area->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	m_scroll_area->setMinimumHeight(260);

	auto *content = new QWidget(m_scroll_area);
	content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	auto *content_layout = new QVBoxLayout(content);
	content_layout->setContentsMargins(0, 0, 4, 0);
	content_layout->setSpacing(8);

	auto configure_form = [](QFormLayout *form) {
		form->setContentsMargins(10, 8, 10, 10);
		form->setHorizontalSpacing(10);
		form->setVerticalSpacing(6);
		form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
		form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	};

	auto configure_combo = [](QComboBox *combo) {
		combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		/* Long canvas/encoder labels must not dictate the dialog width. */
		combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		combo->setMinimumContentsLength(18);
	};

	/* ---------------- Connection ---------------- */
	auto *connection_group = new QGroupBox(tr("Connection"), content);
	auto *connection_form = new QFormLayout(connection_group);
	configure_form(connection_form);

	m_name_edit = new QLineEdit(connection_group);
	m_name_edit->setPlaceholderText(tr("e.g. Facebook, Instagram, TikTok"));
	connection_form->addRow(tr("Name:"), m_name_edit);

	m_template_cb = new QComboBox(connection_group);
	configure_combo(m_template_cb);
	for (const auto &tmpl : s_templates)
		m_template_cb->addItem(tmpl.label);
	connection_form->addRow(tr("Platform template:"), m_template_cb);

	m_server_edit = new QLineEdit(connection_group);
	m_server_edit->setPlaceholderText(tr("rtmp://... or rtmps://..."));
	connection_form->addRow(tr("Server URL:"), m_server_edit);

	auto *key_row = new QHBoxLayout();
	key_row->setContentsMargins(0, 0, 0, 0);
	key_row->setSpacing(6);
	m_key_edit = new QLineEdit(connection_group);
	m_key_edit->setEchoMode(QLineEdit::Password);
	m_key_edit->setPlaceholderText(tr("Stream key"));
	m_show_key_btn = new QPushButton(tr("Show"), connection_group);
	m_show_key_btn->setCheckable(true);
	m_show_key_btn->setMinimumWidth(64);
	key_row->addWidget(m_key_edit, 1);
	key_row->addWidget(m_show_key_btn);
	connection_form->addRow(tr("Stream key:"), key_row);

	auto *import_row = new QHBoxLayout();
	import_row->setContentsMargins(0, 0, 0, 0);
	import_row->setSpacing(6);
	m_import_btn = new QPushButton(tr("Import connection from OBS"), connection_group);
	m_import_btn->setToolTip(
		tr("Copies the active OBS profile's server URL and stream key into this endpoint."));
	m_tiktok_bridge_btn = new QPushButton(tr("Import TikTok Bridge"), connection_group);
	import_row->addWidget(m_import_btn);
	import_row->addWidget(m_tiktok_bridge_btn);
	import_row->addStretch();
	connection_form->addRow(QString(), import_row);
	content_layout->addWidget(connection_group);

	/* ---------------- Video ---------------- */
	auto *video_group = new QGroupBox(tr("Video"), content);
	auto *video_form = new QFormLayout(video_group);
	configure_form(video_form);

	m_video_mode_cb = new QComboBox(video_group);
	configure_combo(m_video_mode_cb);
	m_video_mode_cb->addItem(tr("Use OBS streaming encoder settings"),
	                         static_cast<int>(EncoderSettingsMode::UseOBS));
	m_video_mode_cb->addItem(tr("Custom settings"),
	                         static_cast<int>(EncoderSettingsMode::Custom));
	video_form->addRow(tr("Settings:"), m_video_mode_cb);

	m_codec_cb = new QComboBox(video_group);
	configure_combo(m_codec_cb);
	m_codec_cb->addItem(tr("H.264 / AVC"), static_cast<int>(VideoCodec::H264));
	m_codec_cb->addItem(tr("H.265 / HEVC"), static_cast<int>(VideoCodec::HEVC));
	video_form->addRow(tr("Codec:"), m_codec_cb);

	m_backend_cb = new QComboBox(video_group);
	configure_combo(m_backend_cb);
	video_form->addRow(tr("Encoder:"), m_backend_cb);

	m_bitrate_spin = new QSpinBox(video_group);
	m_bitrate_spin->setRange(500, 50000);
	m_bitrate_spin->setSingleStep(500);
	m_bitrate_spin->setSuffix(tr(" kbps"));
	video_form->addRow(tr("Video bitrate:"), m_bitrate_spin);

	m_keyint_spin = new QSpinBox(video_group);
	m_keyint_spin->setRange(1, 10);
	m_keyint_spin->setSuffix(tr(" s"));
	video_form->addRow(tr("Keyframe interval:"), m_keyint_spin);

	m_orientation_cb = new QComboBox(video_group);
	configure_combo(m_orientation_cb);
	m_orientation_cb->addItem(
		tr("Source / match OBS canvas"),
		static_cast<int>(OutputOrientation::SourceMatch));
	m_orientation_cb->addItem(
		tr("Vertical 1080×1920 — Stretch to full screen"),
		static_cast<int>(OutputOrientation::Vertical1080x1920Stretch));
	m_orientation_cb->addItem(
		tr("Vertical 1080×1920 — Rotate landscape 90° (turn phone sideways)"),
		static_cast<int>(OutputOrientation::Vertical1080x1920Rotated));
	video_form->addRow(tr("Canvas mode:"), m_orientation_cb);

	m_video_hint = new QLabel(video_group);
	m_video_hint->setWordWrap(true);
	m_video_hint->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
	m_video_hint->setStyleSheet("color: palette(mid);");
	video_form->addRow(QString(), m_video_hint);
	content_layout->addWidget(video_group);

	/* ---------------- Audio ---------------- */
	auto *audio_group = new QGroupBox(tr("Audio"), content);
	auto *audio_form = new QFormLayout(audio_group);
	configure_form(audio_form);

	m_audio_mode_cb = new QComboBox(audio_group);
	configure_combo(m_audio_mode_cb);
	m_audio_mode_cb->addItem(tr("Use OBS streaming audio settings"),
	                         static_cast<int>(EncoderSettingsMode::UseOBS));
	m_audio_mode_cb->addItem(tr("Custom AAC"),
	                         static_cast<int>(EncoderSettingsMode::Custom));
	audio_form->addRow(tr("Settings:"), m_audio_mode_cb);

	m_audio_cb = new QComboBox(audio_group);
	configure_combo(m_audio_cb);
	for (int rate : {64, 96, 128, 160, 192, 256, 320})
		m_audio_cb->addItem(QString("%1 kbps").arg(rate), rate);
	audio_form->addRow(tr("Audio bitrate:"), m_audio_cb);
	content_layout->addWidget(audio_group);

	/* ---------------- Behaviour ---------------- */
	auto *behavior_group = new QGroupBox(tr("Start / Stop"), content);
	auto *behavior_layout = new QVBoxLayout(behavior_group);
	behavior_layout->setContentsMargins(10, 8, 10, 10);
	behavior_layout->setSpacing(5);
	m_linked_cb = new QCheckBox(
		tr("Start and stop automatically with OBS main stream"), behavior_group);
	auto *manual_hint = new QLabel(
		tr("When automatic start is disabled, a Start/Stop button appears on the endpoint card."),
		behavior_group);
	manual_hint->setWordWrap(true);
	manual_hint->setStyleSheet("color: palette(mid);");
	behavior_layout->addWidget(m_linked_cb);
	behavior_layout->addWidget(manual_hint);
	content_layout->addWidget(behavior_group);

	m_status_label = new QLabel(content);
	m_status_label->setVisible(false);
	m_status_label->setWordWrap(true);
	m_status_label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
	content_layout->addWidget(m_status_label);

	content_layout->addStretch(1);
	m_scroll_area->setWidget(content);
	outer->addWidget(m_scroll_area, 1);

	/* Sticky footer: never scroll Save/Cancel out of reach. */
	auto *btn_row = new QHBoxLayout();
	btn_row->setContentsMargins(2, 0, 2, 0);
	btn_row->setSpacing(6);
	m_test_btn = new QPushButton(tr("How to Test"), this);
	m_save_btn = new QPushButton(tr("Save"), this);
	m_cancel_btn = new QPushButton(tr("Cancel"), this);
	m_save_btn->setDefault(true);
	btn_row->addWidget(m_test_btn);
	btn_row->addStretch();
	btn_row->addWidget(m_save_btn);
	btn_row->addWidget(m_cancel_btn);
	outer->addLayout(btn_row);

	connect(m_template_cb, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &EndpointDialog::on_template_selected);
	connect(m_show_key_btn, &QPushButton::toggled,
	        this, &EndpointDialog::on_show_key_toggled);
	connect(m_import_btn, &QPushButton::clicked,
	        this, &EndpointDialog::on_import_from_obs);
	connect(m_tiktok_bridge_btn, &QPushButton::clicked,
	        this, &EndpointDialog::on_import_tiktok_bridge);
	connect(m_video_mode_cb, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &EndpointDialog::on_video_mode_changed);
	connect(m_audio_mode_cb, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &EndpointDialog::on_audio_mode_changed);
	connect(m_codec_cb, QOverload<int>::of(&QComboBox::currentIndexChanged),
	        this, &EndpointDialog::on_codec_changed);
	connect(m_test_btn, &QPushButton::clicked,
	        this, &EndpointDialog::on_test_connection);
	connect(m_save_btn, &QPushButton::clicked,
	        this, &EndpointDialog::on_save);
	connect(m_cancel_btn, &QPushButton::clicked,
	        this, &QDialog::reject);
}

void EndpointDialog::showEvent(QShowEvent *event)
{
	QDialog::showEvent(event);

	if (m_initial_geometry_applied)
		return;
	m_initial_geometry_applied = true;

	QScreen *target_screen = screen();
	if (!target_screen && parentWidget())
		target_screen = parentWidget()->screen();
	if (!target_screen)
		target_screen = QGuiApplication::primaryScreen();
	if (!target_screen)
		return;

	const QRect available = target_screen->availableGeometry();

	/* availableGeometry() is expressed in Qt logical pixels, so this
	 * automatically respects Windows DPI scaling and the taskbar. */
	const int edge_margin = 20;
	const int max_width = std::max(1, available.width() - edge_margin * 2);
	const int max_height = std::max(1, available.height() - edge_margin * 2);

	const int target_width = std::min(660, max_width);
	const int target_height = std::min(760, max_height);
	resize(target_width, target_height);

	/* Center inside the usable work area, not the full physical screen. */
	QRect centered(QPoint(0, 0), size());
	centered.moveCenter(available.center());
	move(centered.topLeft());

	if (m_scroll_area && m_scroll_area->verticalScrollBar())
		m_scroll_area->verticalScrollBar()->setValue(0);
}

void EndpointDialog::populate_from_endpoint()
{
	m_name_edit->setText(QString::fromStdString(m_endpoint.name));
	m_server_edit->setText(QString::fromStdString(m_endpoint.server_url));
	m_key_edit->setText(QString::fromStdString(m_endpoint.stream_key));
	m_bitrate_spin->setValue(m_endpoint.video_bitrate_kbps);
	m_keyint_spin->setValue(m_endpoint.keyframe_interval_sec);
	m_linked_cb->setChecked(m_endpoint.linked_to_main);

	auto set_data = [](QComboBox *box, int value) {
		for (int i = 0; i < box->count(); ++i) {
			if (box->itemData(i).toInt() == value) {
				box->setCurrentIndex(i);
				return;
			}
		}
	};

	m_video_mode_cb->blockSignals(true);
	set_data(m_video_mode_cb, static_cast<int>(m_endpoint.video_settings_mode));
	m_video_mode_cb->blockSignals(false);

	m_codec_cb->blockSignals(true);
	set_data(m_codec_cb, static_cast<int>(m_endpoint.video_codec));
	m_codec_cb->blockSignals(false);
	refresh_backend_choices(m_endpoint.encoder_backend);

	set_data(m_audio_mode_cb, static_cast<int>(m_endpoint.audio_settings_mode));
	set_data(m_audio_cb, m_endpoint.audio_bitrate_kbps);
	set_data(m_orientation_cb, static_cast<int>(m_endpoint.orientation));

	m_template_cb->blockSignals(true);
	m_template_cb->setCurrentIndex(0);
	for (int i = 1; i < static_cast<int>(s_templates.size()); ++i) {
		if (m_server_edit->text().startsWith(s_templates[i].url)) {
			m_template_cb->setCurrentIndex(i);
			break;
		}
	}
	m_template_cb->blockSignals(false);

	refresh_mode_controls();
}

void EndpointDialog::refresh_backend_choices(EncoderBackend preferred)
{
	VideoCodec codec = static_cast<VideoCodec>(m_codec_cb->currentData().toInt());
	auto backends = EncoderFactory::available_backends(codec);

	m_backend_cb->blockSignals(true);
	m_backend_cb->clear();
	int selected = -1;
	for (auto backend : backends) {
		int row = m_backend_cb->count();
		m_backend_cb->addItem(
			QString::fromStdString(EncoderFactory::backend_label(backend)),
			static_cast<int>(backend));
		if (backend == preferred)
			selected = row;
	}
	if (selected >= 0)
		m_backend_cb->setCurrentIndex(selected);
	else if (m_backend_cb->count() > 0)
		m_backend_cb->setCurrentIndex(0);
	m_backend_cb->blockSignals(false);
}

void EndpointDialog::refresh_mode_controls()
{
	auto video_mode = static_cast<EncoderSettingsMode>(m_video_mode_cb->currentData().toInt());
	auto audio_mode = static_cast<EncoderSettingsMode>(m_audio_mode_cb->currentData().toInt());
	auto codec = static_cast<VideoCodec>(m_codec_cb->currentData().toInt());

	bool custom_video = video_mode == EncoderSettingsMode::Custom;
	m_codec_cb->setEnabled(custom_video);
	m_backend_cb->setEnabled(custom_video);
	m_bitrate_spin->setEnabled(custom_video);
	m_keyint_spin->setEnabled(custom_video);

	m_audio_cb->setEnabled(audio_mode == EncoderSettingsMode::Custom);

	if (!custom_video) {
		m_video_hint->setText(
			tr("The endpoint clones the video encoder and settings currently configured in OBS. "
			   "Canvas mode remains independent, so vertical output still works."));
	} else if (codec == VideoCodec::HEVC) {
		m_video_hint->setText(
			tr("HEVC/H.265 uses an available hardware encoder. The destination must support "
			   "HEVC over RTMP/Enhanced RTMP; H.264 remains the safest choice for Meta platforms."));
	} else {
		m_video_hint->setText(
			tr("Custom H.264 uses the selected encoder, bitrate and keyframe interval."));
	}
}

Endpoint EndpointDialog::collect_from_form() const
{
	Endpoint ep = m_endpoint;
	ep.name = m_name_edit->text().trimmed().toStdString();
	ep.server_url = m_server_edit->text().trimmed().toStdString();
	/* Validate with trimmed() but preserve the exact key bytes entered. */
	ep.stream_key = m_key_edit->text().toStdString();
	if (!ep.stream_key.empty()) {
		ep.stream_key_decryption_failed = false;
		ep.preserved_protected_stream_key.clear();
	}

	ep.video_settings_mode = static_cast<EncoderSettingsMode>(
		m_video_mode_cb->currentData().toInt());
	ep.video_codec = static_cast<VideoCodec>(m_codec_cb->currentData().toInt());
	if (m_backend_cb->currentIndex() >= 0)
		ep.encoder_backend = static_cast<EncoderBackend>(m_backend_cb->currentData().toInt());
	ep.video_bitrate_kbps = m_bitrate_spin->value();
	ep.keyframe_interval_sec = m_keyint_spin->value();

	ep.audio_settings_mode = static_cast<EncoderSettingsMode>(
		m_audio_mode_cb->currentData().toInt());
	ep.audio_bitrate_kbps = m_audio_cb->currentData().toInt();

	ep.orientation = static_cast<OutputOrientation>(
		m_orientation_cb->currentData().toInt());
	ep.linked_to_main = m_linked_cb->isChecked();
	return ep;
}

void EndpointDialog::on_template_selected(int index)
{
	if (index < 0 || index >= static_cast<int>(s_templates.size()))
		return;
	if (!s_templates[index].url.isEmpty())
		m_server_edit->setText(s_templates[index].url);
}

void EndpointDialog::on_show_key_toggled(bool visible)
{
	m_key_edit->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
	m_show_key_btn->setText(visible ? tr("Hide") : tr("Show"));
}

void EndpointDialog::on_video_mode_changed(int)
{
	refresh_mode_controls();
}

void EndpointDialog::on_audio_mode_changed(int)
{
	refresh_mode_controls();
}

void EndpointDialog::on_codec_changed(int)
{
	EncoderBackend preferred = m_endpoint.encoder_backend;
	if (m_backend_cb->currentIndex() >= 0)
		preferred = static_cast<EncoderBackend>(m_backend_cb->currentData().toInt());
	refresh_backend_choices(preferred);
	refresh_mode_controls();
}

void EndpointDialog::on_import_from_obs()
{
	ObsServiceConfig cfg = import_from_active_obs_profile();
	if (!cfg.ok) {
		QMessageBox::warning(
			this, tr("Import from OBS"),
			tr("Could not import from active OBS profile:\n\n%1")
				.arg(QString::fromStdString(cfg.error_message)));
		return;
	}

	m_server_edit->setText(QString::fromStdString(cfg.server_url));
	m_key_edit->setText(QString::fromStdString(cfg.stream_key));

	if (m_name_edit->text().trimmed().isEmpty() ||
	    m_name_edit->text() == tr("New Endpoint")) {
		QString svc = QString::fromStdString(cfg.service_name);
		if (!svc.isEmpty())
			m_name_edit->setText(svc + tr(" (imported)"));
	}

	for (int i = 1; i < static_cast<int>(s_templates.size()); ++i) {
		if (QString::fromStdString(cfg.server_url).startsWith(s_templates[i].url)) {
			m_template_cb->blockSignals(true);
			m_template_cb->setCurrentIndex(i);
			m_template_cb->blockSignals(false);
			break;
		}
	}

	m_status_label->setText(
		tr("✓ Connection imported from OBS: %1")
			.arg(QString::fromStdString(cfg.service_name)));
	m_status_label->setStyleSheet("color: #2ecc71;");
	m_status_label->setVisible(true);
}

void EndpointDialog::on_import_tiktok_bridge()
{
	QString path = QString::fromStdString(default_tiktok_bridge_path());
	if (path.isEmpty() || !QFileInfo::exists(path)) {
		path = QFileDialog::getOpenFileName(
			this, tr("Import TikTok Bridge JSON"), path,
			tr("JSON files (*.json);;All files (*)"));
		if (path.isEmpty())
			return;
	}

	TikTokBridgeConfig cfg = import_tiktok_bridge_file(path.toStdString());
	if (!cfg.ok) {
		QMessageBox::warning(
			this, tr("Import TikTok Bridge"),
			tr("Could not import TikTok Bridge data:\n\n%1")
				.arg(QString::fromStdString(cfg.error_message)));
		return;
	}

	m_server_edit->setText(QString::fromStdString(cfg.server_url));
	m_key_edit->setText(QString::fromStdString(cfg.stream_key));

	if (m_name_edit->text().trimmed().isEmpty() ||
	    m_name_edit->text() == tr("New Endpoint")) {
		QString bridge_name = QString::fromStdString(cfg.name).trimmed();
		m_name_edit->setText(bridge_name.isEmpty() ? tr("TikTok Bridge") : bridge_name);
	}

	/* Safe TikTok defaults: custom H.264 + portrait stretch. */
	for (int i = 0; i < m_video_mode_cb->count(); ++i)
		if (m_video_mode_cb->itemData(i).toInt() == static_cast<int>(EncoderSettingsMode::Custom))
			m_video_mode_cb->setCurrentIndex(i);
	for (int i = 0; i < m_codec_cb->count(); ++i)
		if (m_codec_cb->itemData(i).toInt() == static_cast<int>(VideoCodec::H264))
			m_codec_cb->setCurrentIndex(i);
	for (int i = 0; i < m_orientation_cb->count(); ++i)
		if (m_orientation_cb->itemData(i).toInt() ==
		    static_cast<int>(OutputOrientation::Vertical1080x1920Stretch))
			m_orientation_cb->setCurrentIndex(i);

	m_bitrate_spin->setValue(2500);
	for (int i = 0; i < m_audio_cb->count(); ++i)
		if (m_audio_cb->itemData(i).toInt() == 128)
			m_audio_cb->setCurrentIndex(i);

	m_status_label->setText(
		tr("✓ TikTok Bridge imported from %1").arg(QFileInfo(path).fileName()));
	m_status_label->setStyleSheet("color: #2ecc71;");
	m_status_label->setVisible(true);
}

void EndpointDialog::on_test_connection()
{
	m_status_label->setText(
		tr("A real RTMP test necessarily authenticates/publishes to the destination. "
		   "Disable automatic start, save the endpoint, then use its manual Start button. "
		   "The Health tab shows the actual server result."));
	m_status_label->setVisible(true);
}

void EndpointDialog::on_save()
{
	if (m_name_edit->text().trimmed().isEmpty()) {
		QMessageBox::warning(this, tr("Validation"), tr("Please enter an endpoint name."));
		return;
	}
	if (m_server_edit->text().trimmed().isEmpty()) {
		QMessageBox::warning(this, tr("Validation"), tr("Please enter a server URL."));
		return;
	}
	if (m_key_edit->text().trimmed().isEmpty()) {
		QMessageBox::warning(this, tr("Validation"), tr("Please enter a stream key."));
		return;
	}

	const QUrl server_url(m_server_edit->text().trimmed());
	const QString scheme = server_url.scheme().toLower();
	if (!server_url.isValid() || server_url.host().trimmed().isEmpty() ||
	    (scheme != "rtmp" && scheme != "rtmps")) {
		QMessageBox::warning(
			this, tr("Validation"),
			tr("Server URL must be a valid rtmp:// or rtmps:// address."));
		return;
	}

	auto video_mode = static_cast<EncoderSettingsMode>(m_video_mode_cb->currentData().toInt());
	if (video_mode == EncoderSettingsMode::Custom && m_backend_cb->currentIndex() < 0) {
		QMessageBox::warning(
			this, tr("Validation"),
			tr("No encoder is available for the selected codec on this computer."));
		return;
	}

	m_result = collect_from_form();
	accept();
}

} // namespace smulti
