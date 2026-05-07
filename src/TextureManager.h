#pragma once

#include <QOpenGLTexture>
#include <QString>
#include <QVector4D>
#include <gli/gli.hpp>
#include <map>
#include <string>

class TextureManager
{
public:
  explicit TextureManager(QString sourceFileName);
  ~TextureManager()                                = default;
  TextureManager(const TextureManager&)            = delete;
  TextureManager(TextureManager&&)                 = delete;
  TextureManager& operator=(const TextureManager&) = delete;
  TextureManager& operator=(TextureManager&&)      = delete;

  void cleanup();

  QOpenGLTexture* getTexture(const std::string& texturePath);
  QOpenGLTexture* getTexture(const QString& texturePath);

  QOpenGLTexture* getErrorTexture();
  QOpenGLTexture* getBlackTexture();
  QOpenGLTexture* getWhiteTexture();
  QOpenGLTexture* getFlatNormalTexture();

private:
  [[nodiscard]] QOpenGLTexture* loadTexture(QString texturePath) const;
  static QOpenGLTexture* makeTexture(const gli::texture& texture);
  static QOpenGLTexture* makeSolidColor(QVector4D color);

  QString resolvePath(QString path) const;

  QString m_SourceFileName;
  QString m_DataRoot;
  QOpenGLTexture* m_ErrorTexture      = nullptr;
  QOpenGLTexture* m_BlackTexture      = nullptr;
  QOpenGLTexture* m_WhiteTexture      = nullptr;
  QOpenGLTexture* m_FlatNormalTexture = nullptr;

  std::map<std::string, QOpenGLTexture*> m_Textures;
};
