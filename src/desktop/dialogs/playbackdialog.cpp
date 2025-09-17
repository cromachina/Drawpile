// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/dialogs/playbackdialog.h"
#include "desktop/dialogs/videoexportdialog.h"
#include "desktop/mainwindow.h"
#include "desktop/utils/widgetutils.h"
#include "libclient/canvas/canvasmodel.h"
#include "libclient/canvas/indexbuilderrunnable.h"
#include "libclient/canvas/paintengine.h"
#include "libclient/export/videoexporter.h"
#include "ui_playback.h"
#include <QCloseEvent>
#include <QIcon>
#include <QMessageBox>
#include <QThreadPool>
#include <QTimer>

namespace dialogs {

static constexpr double PLAY_FPS = 30.0;
static constexpr double PLAY_MSECS = 1000.0 / PLAY_FPS;
static constexpr double PLAY_MSECS_MAX = 100.0;

PlaybackDialog::PlaybackDialog(canvas::CanvasModel *canvas, QWidget *parent)
	: QDialog{parent}
	, m_ui{new Ui_PlaybackDialog}
	, m_paintengine{canvas->paintEngine()}
	, m_speed{1.0}
	, m_haveIndex{false}
	, m_autoplay{false}
	, m_awaiting{false}
{
	setWindowTitle(tr("Playback"));
	setWindowFlags(Qt::Tool);
	setMinimumSize(200, 80);
	resize(420, 250);
	m_ui->setupUi(this);

	m_ui->buildIndexProgress->hide();

	connect(
		m_ui->buildIndexButton, &QAbstractButton::clicked, this,
		&PlaybackDialog::onBuildIndexClicked);
#ifdef HAVE_VIDEO_EXPORT
	connect(
		m_ui->configureExportButton, &QAbstractButton::clicked, this,
		&PlaybackDialog::onVideoExportClicked);
#else
	m_ui->configureExportButton->setVisible(false);
#endif

	m_playTimer = new QTimer{this};
	m_playTimer->setTimerType(Qt::PreciseTimer);
	m_playTimer->setSingleShot(true);
	connect(m_playTimer, &QTimer::timeout, this, [this]() {
		playNext(getNextMsecs());
	});

	m_ui->speedSpinner->setExponentRatio(3.0);
	connect(
		m_ui->speedSpinner,
		QOverload<double>::of(&KisDoubleSliderSpinBox::valueChanged), this,
		[this](double speed) {
			m_speed = speed / 100.0;
		});

	// The paint engine's playback callback lets us know when the step/sequence
	// has been rendered and we're free to take another one.
	connect(
		canvas->paintEngine(), &canvas::PaintEngine::playbackAt, this,
		&PlaybackDialog::onPlaybackAt, Qt::QueuedConnection);

	connect(
		m_ui->firstButton, &QAbstractButton::clicked, this,
		&PlaybackDialog::skipBeginning);
	connect(
		m_ui->prevSkipButton, &QAbstractButton::clicked, this,
		&PlaybackDialog::skipPreviousSnapshot);
	connect(
		m_ui->playButton, &QAbstractButton::toggled, this,
		&PlaybackDialog::setPlaying);
	connect(
		m_ui->nextButton, &QAbstractButton::clicked, this,
		&PlaybackDialog::skipNextStroke);
	connect(
		m_ui->nextSkipButton, &QAbstractButton::clicked, this,
		&PlaybackDialog::skipForward);
	connect(
		m_ui->filmStrip, &widgets::Filmstrip::doubleClicked, this,
		&PlaybackDialog::jumpTo);
	connect(
		m_ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

	utils::setWidgetRetainSizeWhenHidden(m_ui->prevSkipButton, true);
	m_ui->prevSkipButton->setVisible(false);

	loadIndex();
}

PlaybackDialog::~PlaybackDialog()
{
	delete m_ui;
}

/**
 * @brief The paint engine has finished rendering the sequence
 *
 * We can now automatically step forward again
 */
void PlaybackDialog::onPlaybackAt(long long pos)
{
	m_awaiting = false;

	bool atEnd = pos < 0;
	if(atEnd) {
		setPlaying(false);
		m_ui->playbackProgress->setValue(m_ui->playbackProgress->maximum());
		m_ui->filmStrip->setCursor(m_ui->filmStrip->length());
	} else {
		m_ui->playbackProgress->setValue(pos);
		m_ui->filmStrip->setCursor(pos);
	}

	updateButtons();

#ifdef HAVE_VIDEO_EXPORT
	if(!atEnd && m_exporter && m_ui->autoSaveFrame->isChecked()) {
		exportFrame(1);
		return;
	}
#endif
	if(m_autoplay) {
		double elapsed = m_lastFrameTime.elapsed();
		if(elapsed < PLAY_MSECS) {
			m_playTimer->start(PLAY_MSECS - elapsed);
		} else {
			playNext(qMin(PLAY_MSECS_MAX, elapsed));
		}
	}
}

#ifdef HAVE_VIDEO_EXPORT
void PlaybackDialog::onExporterReady()
{
	m_ui->saveFrame->setEnabled(true);
	m_ui->frameLabel->setText(QString::number(m_exporter->frame()));
	if(m_autoplay) {
		double elapsed = m_lastFrameTime.elapsed();
		double msecs = 1000.0 / m_exporter->fps();
		if(elapsed < msecs) {
			m_playTimer->start(msecs - elapsed);
		} else {
			playNext(msecs);
		}
	}
}
#endif

void PlaybackDialog::playNext(double msecs)
{
	playbackCommand([=]() {
		m_lastFrameTime.restart();
		return m_paintengine->playPlayback(qRound(msecs * m_speed));
	});
}

void PlaybackDialog::jumpTo(int pos)
{
	playbackCommand([=]() {
		setPlaying(false);
		return m_paintengine->jumpPlaybackTo(pos);
	});
}

void PlaybackDialog::onBuildIndexClicked()
{
	m_ui->noIndexReason->setText(tr("Building index..."));
	m_ui->buildIndexProgress->show();
	m_ui->buildIndexButton->setEnabled(false);

	canvas::IndexBuilderRunnable *indexer =
		new canvas::IndexBuilderRunnable(m_paintengine);
	connect(
		indexer, &canvas::IndexBuilderRunnable::progress,
		m_ui->buildIndexProgress, &QProgressBar::setValue);
	connect(
		indexer, &canvas::IndexBuilderRunnable::indexingComplete, this,
		[this](bool success, QString error) {
			m_ui->buildIndexProgress->hide();
			if(success) {
				loadIndex();
			} else {
				qWarning("Error building index: %s", qUtf8Printable(error));
				m_ui->noIndexReason->setText(tr("Index building failed."));
			}
		});

	QThreadPool::globalInstance()->start(indexer);
}

void PlaybackDialog::loadIndex()
{
	if(!m_paintengine->loadPlaybackIndex()) {
		qWarning("Error loading index: %s", DP_error());
		return;
	}

	m_haveIndex = true;
	m_ui->filmStrip->setLength(m_paintengine->playbackIndexMessageCount());
	m_ui->filmStrip->setFrames(m_paintengine->playbackIndexEntryCount());

	m_ui->prevSkipButton->setVisible(true);
	m_ui->buildIndexButton->hide();
	m_ui->buildIndexProgress->hide();
	m_ui->noIndexReason->hide();

	m_ui->filmStrip->setLoadImageFn([this](int frame) {
		return m_paintengine->playbackIndexThumbnailAt(frame);
	});

	m_ui->indexStack->setCurrentIndex(1);
}

void PlaybackDialog::centerOnParent()
{
	if(parentWidget()) {
		QRect parentG = parentWidget()->geometry();
		QRect myG = geometry();

		move(
			parentG.x() + parentG.width() / 2 - myG.width() / 2,
			parentG.y() + parentG.height() / 2 - myG.height() / 2);
	}
}

bool PlaybackDialog::isPlaying() const
{
	return m_autoplay;
}

void PlaybackDialog::setPlaying(bool playing)
{
	m_playTimer->stop();
	if(playing) {
		DP_PlayerResult result = m_paintengine->beginPlayback();
		if(isErrorResult(result)) {
			qWarning("Error starting playback: %s", DP_error());
		}
		m_autoplay = result == DP_PLAYER_SUCCESS;
	} else {
		m_autoplay = false;
	}

	updateButtons();
	if(m_autoplay) {
		playNext(PLAY_MSECS);
	}

	emit playbackToggled(m_autoplay);
}

void PlaybackDialog::skipBeginning()
{
	jumpTo(0);
}

void PlaybackDialog::skipPreviousSnapshot()
{
	playbackCommand([this]() {
		setPlaying(false);
		return m_paintengine->skipPlaybackBy(-1, true);
	});
}

void PlaybackDialog::skipNextStroke()
{
	playbackCommand([this]() {
		setPlaying(false);
		return m_paintengine->skipPlaybackBy(1, false);
	});
}

void PlaybackDialog::skipForward()
{
	playbackCommand([this]() {
		setPlaying(false);
		return m_haveIndex ? m_paintengine->skipPlaybackBy(1, true)
						   : m_paintengine->stepPlayback(10000);
	});
}

void PlaybackDialog::closeEvent(QCloseEvent *event)
{
	m_paintengine->closePlayback();
#ifdef HAVE_VIDEO_EXPORT
	if(m_exporter) {
		// Exporter still working? Disown it and let it finish.
		// It will delete itself once done.
		m_exporter->setParent(nullptr);
		m_exporter->finish();
	}
#endif
	QDialog::closeEvent(event);
}

void PlaybackDialog::keyPressEvent(QKeyEvent *event)
{
	// This is not a OK/Cancel type dialog, so disable
	// key events. Without this, it is easy to close
	// the window accidentally by hitting Esc.
	event->ignore();
}

#ifdef HAVE_VIDEO_EXPORT
void PlaybackDialog::onVideoExportClicked()
{
	VideoExportDialog *dlg = new VideoExportDialog(this);
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	utils::makeModal(dlg);
	connect(dlg, &QDialog::accepted, this, [this, dlg]() {
		VideoExporter *exporter = dlg->getExporter();
		if(exporter) {
			startVideoExport(exporter);
		}
	});
	dlg->show();
}

void PlaybackDialog::startVideoExport(VideoExporter *exporter)
{
	m_exporter = exporter;
	m_exporter->setParent(this);

	m_ui->exportStack->setCurrentIndex(0);
	m_ui->saveFrame->setEnabled(true);

	connect(
		m_exporter, &VideoExporter::exporterFinished, this,
		&PlaybackDialog::videoExporterFinished);
	connect(
		m_exporter, &VideoExporter::exporterError, this,
		&PlaybackDialog::videoExporterError);
	connect(
		m_ui->saveFrame, &QAbstractButton::clicked, this,
		&PlaybackDialog::exportFrame);
	connect(
		m_ui->stopExport, &QAbstractButton::clicked, m_exporter,
		&VideoExporter::finish);
	connect(
		m_exporter, &VideoExporter::exporterReady, this,
		&PlaybackDialog::onExporterReady);

	m_exporter->start();
}

void PlaybackDialog::videoExporterError(const QString &msg)
{
	setPlaying(false);
	utils::showWarning(this, tr("Video error"), msg);
}

void PlaybackDialog::videoExporterFinished(bool showExportDialogAgain)
{
	setPlaying(false);
	m_ui->exportStack->setCurrentIndex(1);

	if(m_exporter) {
		m_exporter->deleteLater();
		m_exporter = nullptr;
	}

	if(showExportDialogAgain) {
		QMetaObject::invokeMethod(
			this, "onVideoExportClicked", Qt::QueuedConnection);
	}
}

void PlaybackDialog::exportFrame(int count)
{
	Q_ASSERT(m_exporter);
	count = qMax(1, count);

	QImage image = m_paintengine->renderPixmap();
	if(image.isNull()) {
		qWarning("exportFrame: image is null!");
		onExporterReady();
	} else {
		m_ui->saveFrame->setEnabled(false);
		m_exporter->saveFrame(image, count);
	}
}
#endif

double PlaybackDialog::getNextMsecs()
{
#ifdef HAVE_VIDEO_EXPORT
	if(m_exporter) {
		return 1000.0 / m_exporter->fps();
	}
#endif
	return qMin(PLAY_MSECS_MAX, double(m_lastFrameTime.elapsed()));
}

void PlaybackDialog::playbackCommand(std::function<DP_PlayerResult()> fn)
{
	if(!m_awaiting) {
		m_awaiting = true;
		if(isErrorResult(fn())) {
			qWarning("Error: %s", DP_error());
		}
	}
}

void PlaybackDialog::updateButtons()
{
	m_ui->firstButton->setDisabled(m_awaiting || m_autoplay);
	m_ui->prevSkipButton->setDisabled(m_awaiting || m_autoplay);

	m_ui->playButton->setDisabled(m_awaiting && !m_autoplay);
	m_ui->playButton->setChecked(m_autoplay);
	m_ui->playButton->setIcon(QIcon::fromTheme(
		m_autoplay ? "media-playback-pause" : "media-playback-start"));

	m_ui->nextButton->setDisabled(m_awaiting || m_autoplay);
	m_ui->nextSkipButton->setDisabled(m_awaiting || m_autoplay);
}

bool PlaybackDialog::isErrorResult(DP_PlayerResult result)
{
	return result != DP_PLAYER_SUCCESS && result != DP_PLAYER_RECORDING_END;
}

}
