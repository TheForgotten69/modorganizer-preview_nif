#include <NifFile.hpp>

#include "NifExtensions.h"
#include "NifWidget.h"
#include "PreviewNif.h"

#include <QDebug>
#include <QDir>
#include <QGridLayout>
#include <QHash>
#include <QSet>
#include <exception>
#include <filesystem>
#include <sstream>

namespace
{
QString textureKey(const QString& value)
{
  return QDir::cleanPath(QDir::fromNativeSeparators(value)).toCaseFolded();
}

QString toQString(const std::string& value)
{
  return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

QSet<QString> collectTexturePaths(nifly::NifFile* nifFile)
{
  QSet<QString> texturePaths;

  for (const auto& shape : nifFile->GetShapes()) {
    const auto shader = nifFile->GetShader(shape);
    if (!shader || !shader->HasTextureSet()) {
      continue;
    }

    const auto textureSetRef = shader->TextureSetRef();
    const auto textureSet = nifFile->GetHeader().GetBlock(textureSetRef);
    if (!textureSet) {
      continue;
    }

    for (const auto& texture : textureSet->textures) {
      const auto path = toQString(texture.get());
      if (!path.isEmpty()) {
        texturePaths.insert(path);
      }
    }
  }

  return texturePaths;
}

QHash<QString, QString> resolveTexturePaths(MOBase::IOrganizer* organizer,
                                            nifly::NifFile* nifFile)
{
  QHash<QString, QString> resolvedTexturePaths;
  if (!organizer) {
    return resolvedTexturePaths;
  }

  const auto texturePaths = collectTexturePaths(nifFile);
  qInfo() << "Pre-resolving" << texturePaths.size()
          << "NIF texture path(s) before OpenGL setup";

  for (const auto& texturePath : texturePaths) {
    const auto normalizedTexturePath =
        QDir::cleanPath(QDir::fromNativeSeparators(texturePath));

    QString resolved;
    try {
      resolved = organizer->resolvePath(texturePath);
      if (resolved.isEmpty() && normalizedTexturePath != texturePath) {
        resolved = organizer->resolvePath(normalizedTexturePath);
      }
    } catch (const std::exception& e) {
      qWarning() << "Failed to pre-resolve NIF texture" << texturePath
                 << e.what();
      continue;
    } catch (...) {
      qWarning() << "Failed to pre-resolve NIF texture" << texturePath
                 << "unknown exception";
      continue;
    }

    if (resolved.isEmpty()) {
      qInfo() << "NIF texture pre-resolve missing" << texturePath;
      continue;
    }

    qInfo() << "NIF texture pre-resolved" << texturePath << "->" << resolved;
    resolvedTexturePaths.insert(texturePath, resolved);
    resolvedTexturePaths.insert(normalizedTexturePath, resolved);
    resolvedTexturePaths.insert(textureKey(texturePath), resolved);
  }

  return resolvedTexturePaths;
}

}

bool PreviewNif::init(MOBase::IOrganizer* moInfo)
{
  m_MOInfo = moInfo;
  return true;
}

QString PreviewNif::name() const
{
  return "Preview NIF";
}

QString PreviewNif::author() const
{
  return "Parapets";
}

QString PreviewNif::description() const
{
  return "Supports previewing NIF files";
}

MOBase::VersionInfo PreviewNif::version() const
{
  return {0, 4, 3, 0, MOBase::VersionInfo::RELEASE_BETA};
}

QList<MOBase::PluginSetting> PreviewNif::settings() const
{
  return {};
}

bool PreviewNif::enabledByDefault() const
{
  return true;
}

std::set<QString> PreviewNif::supportedExtensions() const
{
  return {"bto", "btr", "nif"};
}

bool PreviewNif::supportsArchives() const
{
  return true;
}

QWidget* PreviewNif::genFilePreview(const QString& fileName, const QSize& maxSize) const
{
  return genDataPreview(nullptr, fileName, maxSize);
}

QWidget* PreviewNif::genDataPreview(const QByteArray& fileData, const QString& fileName,
                                    const QSize& maxSize) const
{
  auto path = std::filesystem::path(fileName.toStdWString());
  std::shared_ptr<nifly::NifFile> nifFile;

  qInfo() << "NIF preview requested for" << fileName << "max size" << maxSize
          << "data bytes" << (fileData == nullptr ? 0 : fileData.size());

  if (fileData != nullptr && !fileData.isEmpty()) {
    qInfo() << "Loading NIF preview from provided data for" << fileName;
    const auto fileStream =
        std::make_shared<std::istringstream>(fileData.toStdString());
    nifFile = std::make_shared<nifly::NifFile>(*fileStream);
  } else {
    qInfo() << "Loading NIF preview from file path" << fileName;
    nifFile = std::make_shared<nifly::NifFile>(path);
  }

  if (!nifFile->IsValid()) {
    qWarning(qUtf8Printable(tr("Failed to load file: %1").arg(fileName)));
    return nullptr;
  }

  const auto layout = new QGridLayout();
  layout->setRowStretch(0, 1);
  layout->setColumnStretch(0, 1);

  layout->addWidget(makeLabel(nifFile.get()), 1, 0, 1, 1);

  constexpr bool logGlErrors = true;
  qInfo("NIF preview OpenGL diagnostic logging enabled");
  const auto resolvedTexturePaths = resolveTexturePaths(m_MOInfo, nifFile.get());

  const auto nifWidget =
      new NifWidget(nifFile, fileName, resolvedTexturePaths, m_MOInfo, logGlErrors);
  layout->addWidget(nifWidget, 0, 0, 1, 1);

  const auto widget = new QWidget();
  widget->setLayout(layout);
  return widget;
}

QLabel* PreviewNif::makeLabel(const nifly::NifFile* nifFile)
{
  unsigned int shapes = 0;
  unsigned int faces  = 0;
  unsigned int verts  = 0;

  for (const auto& shape : nifFile->GetShapes()) {
    shapes++;
    faces += shape->GetNumTriangles();
    verts += shape->GetNumVertices();
  }

  qInfo() << "NIF preview summary" << verts << "verts" << faces << "faces"
          << shapes << "shape(s)";

  const auto text =
      tr("Verts: %1 | Faces: %2 | Shapes: %3").arg(verts).arg(faces).arg(shapes);
  const auto label = new QLabel(text);
  label->setWordWrap(true);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  return label;
}
