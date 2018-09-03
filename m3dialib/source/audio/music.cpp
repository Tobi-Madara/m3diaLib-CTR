#include <cstring>
#include <string>
#include "audio/music.hpp"
#include "private/private.hpp"

namespace m3d {
    Music::Music(const std::string& t_filename) :
            m_position(0),
            m_volume(1.f),
            m_started(false),
            m_loop(false),
            m_volumeChanged(false),
            m_reader(nullptr) {
        setFile(t_filename);
    }

    Music::~Music() {
        stop();

        if (!m_started) {
            m_thread.initialize([](m3d::Parameter){}, nullptr);
            m_thread.start();
        }

        delete m_reader;
    }

    void Music::setFile(const std::string& t_filename) {
        stop();
        m_file = t_filename;
        m_filetype = getFileType(m_file);

        switch(m_filetype) {
            case m3d::Music::FileType::Mp3:
                delete m_reader;
                m_reader = new m3d::Playable::Mp3Reader;
                m_reader->set(m_decoder);
                break;

            case m3d::Music::FileType::Wav:
                delete m_reader;
                m_reader = new m3d::Playable::WavReader;
                m_reader->set(m_decoder);
                break;

            default:
                delete m_reader;
                m_reader = nullptr;
        }
    }

    const std::string& Music::getFile() {
        return m_file;
    }

    void Music::play(bool t_waitForChannel) {
        if (m_filetype != m3d::Playable::FileType::Error) {
            m_started = true;

            if (m_status == m3d::Music::Status::Stopped) {
                m_status = m3d::Music::Status::Playing;
                m_thread.initialize(std::bind(&m3d::Music::playFile, this, std::placeholders::_1), t_waitForChannel);
                m_thread.start();

                for (const auto& callback: m_playCallbacks) {
                    callback();
                }
            } else if (m_status == m3d::Music::Status::Paused) {
                m_status = m3d::Music::Status::Playing;

                for (const auto& callback: m_playCallbacks) {
                    callback();
                }
            }
        }
    }

    void Music::pause() {
        if (m_status != m3d::Music::Status::Paused) {
            m_status = m3d::Music::Status::Paused;

            for (const auto& callback: m_pauseCallbacks) {
                callback();
            }
        }
    }

    void Music::stop() {
        if (m_status != m3d::Music::Status::Stopped) {
            m_status = m3d::Music::Status::Stopped;
            m_thread.join();
            m_position = 0;
            m_decoder.reset();

            for (const auto& callback: m_stopCallbacks) {
                callback(false);
            }
        }
    }

    void Music::togglePlay() {
        if (m_status == m3d::Music::Status::Paused) {
            m_status = m3d::Music::Status::Playing;

            for (const auto& callback: m_playCallbacks) {
                callback();
            }
        } else if (m_status == m3d::Music::Status::Playing) {
            m_status = m3d::Music::Status::Paused;

            for (const auto& callback: m_pauseCallbacks) {
                callback();
            }
        }
    }

    m3d::Music::Status Music::getPlayStatus() {
        return m_status;
    }
    void Music::setVolume(float t_volume) {
        if (t_volume > 1.f) {
            m_volume = 1.f;
        } else if (t_volume < 0) {
            m_volume = 0.f;
        } else {
            m_volume = t_volume;
        }

        m_volumeChanged = true;
    }

    float Music::getVolume() {
        return m_volume;
    }

    void Music::loop(bool t_loop) {
        m_loop = t_loop;
        m_volumeChanged = true;
    }

    bool Music::getLoop() {
        return m_loop;
    }

    void Music::onPause(std::function<void()> t_callback) {
        m_pauseCallbacks.push_back(t_callback);
    }

    void Music::onLoop(std::function<void()> t_callback) {
        m_loopCallbacks.push_back(t_callback);
    }

    void Music::onStop(std::function<void(bool)> t_callback) {
        m_stopCallbacks.push_back(t_callback);
    }

    // private methods
    void Music::playFile(m3d::Parameter t_waitForChannel) {
        // ndsp wasn't initialized or there was an error
        if (!m3d::priv::ndsp::initialized) {
            m_status = m3d::Music::Status::Stopped;
            return;
        }

        int channel = -1;

        if (m3d::priv::ndsp::channelsFree()) {
            channel = m3d::priv::ndsp::occupyChannel();
        } else {
            if (t_waitForChannel.get<bool>()) {
                while (!m3d::priv::ndsp::channelsFree());
                channel = m3d::priv::ndsp::occupyChannel();
            } else {
                // no free channel
                return;
            }
        }

        int16_t *buffer1, *buffer2;
        ndspWaveBuf waveBuf[2];
        bool lastbuffer = false;
        std::string file;

        {
            m3d::Lock lock(m_mutex);
            file = m_file;
        }

        if(m_decoder.init(file.c_str()) != 0) {
            return;
        }

        if(m_decoder.getChannels() > 2 || m_decoder.getChannels() < 1) {
            return;
        }

        buffer1 = static_cast<int16_t*>(linearAlloc(m_decoder.m_buffSize * sizeof(int16_t)));
        buffer2 = static_cast<int16_t*>(linearAlloc(m_decoder.m_buffSize * sizeof(int16_t)));

        ndspChnReset(channel);
        ndspChnWaveBufClear(channel);
        ndspSetOutputMode(NDSP_OUTPUT_STEREO);
        ndspChnSetInterp(channel, NDSP_INTERP_POLYPHASE);
        ndspChnSetRate(channel, m_decoder.getRate());
        ndspChnSetFormat(channel,
                m_decoder.getChannels() == 2 ? NDSP_FORMAT_STEREO_PCM16 :
                NDSP_FORMAT_MONO_PCM16);

        float volume[12];
        for (int i = 0; i < 12; i++) {
            volume[i] = m_volume;
        }

        ndspChnSetMix(channel, volume);

        memset(waveBuf, 0, sizeof(waveBuf));
        waveBuf[0].nsamples = m_decoder.decode(&buffer1[0]) / m_decoder.getChannels();
        waveBuf[0].data_vaddr = &buffer1[0];
        ndspChnWaveBufAdd(channel, &waveBuf[0]);

        waveBuf[1].nsamples = m_decoder.decode(&buffer2[0]) / m_decoder.getChannels();
        waveBuf[1].data_vaddr = &buffer2[0];
        ndspChnWaveBufAdd(channel, &waveBuf[1]);

        // wait for music to start
        while (ndspChnIsPlaying(channel) == false);

        while (m_status != m3d::Music::Status::Stopped || m_loop) {
            if (m_status == m3d::Music::Status::Paused
                && !ndspChnIsPaused(channel)) {
                ndspChnSetPaused(channel, true);
            } else if (m_status == m3d::Music::Status::Playing
                && ndspChnIsPaused(channel)) {
                ndspChnSetPaused(channel, false);
            }

            if (m_volumeChanged) {
                for (int i = 0; i < 12; i++) {
                    volume[i] = m_volume;
                }

                ndspChnSetMix(channel, volume);
            }

            svcSleepThread(100 * 1000);

            // break after the last buffer has finished
            if(lastbuffer == true && waveBuf[0].status == NDSP_WBUF_DONE &&
                    waveBuf[1].status == NDSP_WBUF_DONE) {
                m_status = m3d::Music::Status::Stopped;
                break;
            }

            if(ndspChnIsPaused(channel) == true || lastbuffer == true) continue;

            if(waveBuf[0].status == NDSP_WBUF_DONE) {
                size_t read = m_decoder.decode(&buffer1[0]);

                if(read <= 0) {
                    if (m_loop) {
                        m_decoder.reset();
                    } else {
                        lastbuffer = true;
                        m_status = m3d::Music::Status::Stopped;
                    }
                    continue;
                } else if(read < m_decoder.m_buffSize) {
                    waveBuf[0].nsamples = read / m_decoder.getChannels();
                }

                ndspChnWaveBufAdd(channel, &waveBuf[0]);
            }

            if(waveBuf[1].status == NDSP_WBUF_DONE) {
                size_t read = m_decoder.decode(&buffer2[0]);

                if(read <= 0) {
                    if (m_loop) {
                        m_decoder.reset();
                    } else {
                        lastbuffer = true;
                        m_status = m3d::Music::Status::Stopped;
                    }
                    continue;
                } else if(read < m_decoder.m_buffSize) {
                    waveBuf[1].nsamples = read / m_decoder.getChannels();
                }

                ndspChnWaveBufAdd(channel, &waveBuf[1]);
            }

            DSP_FlushDataCache(buffer1, m_decoder.m_buffSize * sizeof(int16_t));
            DSP_FlushDataCache(buffer2, m_decoder.m_buffSize * sizeof(int16_t));
        }
        std::cout << "Playing stopped" << '\n';
        m_decoder.exit();

        ndspChnWaveBufClear(channel);
        linearFree(buffer1);
        linearFree(buffer2);
        m3d::priv::ndsp::freeChannel(channel);
    }
}; /* m3d */
