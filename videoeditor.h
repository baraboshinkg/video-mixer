#ifndef VIDEOEDITOR_H
#define VIDEOEDITOR_H

#include <QWidget>

#include <QGst/Message>
#include <QGst/Pipeline>
#include <QGst/Ui/VideoWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QSlider;
class QTimer;
QT_END_NAMESPACE

class VideoEditor : public QWidget
{
    Q_OBJECT
public:
    explicit VideoEditor(QWidget *parent = 0);
    ~VideoEditor();
    void loadFile(const QString& filePath);

protected:
    virtual void closeEvent(QCloseEvent *);

signals:

public slots:
    void setPosition(int position);
    void onSeekClick();
    void onPlayPauseClick();
    void onPositionChanged();

private:
    QSlider*               sliderPos;
    QLabel*                lblPos;
    QLabel*                lblRange;
    QGst::Ui::VideoWidget* videoWidget;
    QGst::PipelinePtr      pipeline;
    QTimer*                positionTimer;

    void onBusMessage(const QGst::MessagePtr& message);
    void onStateChange(const QGst::StateChangedMessagePtr& message);
};

#endif // VIDEOEDITOR_H
