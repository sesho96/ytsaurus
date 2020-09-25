package jobfs

import (
	"errors"
	"fmt"
	"path/filepath"
	"strings"
)

// Config описывает загрузку файлов на YT и сохранение результатов запуска.
type Config struct {
	// UploadFile задаёт список файлов, которые нужно загрузить на YT. Каждый файл заливается на YT
	// отдельным запросом. Результат загрузки кешируется на основе md5-хеша файла.
	UploadFile []string `json:"upload_file"`
	// UploadTarDir задаёт список директорий, которые будут загружены на YT вместе со всем содержимым,
	// включая файлы и симлинки. Каждая директория из списка загружается отдельным tar-архивом и кешируется
	// на основе md5-хеша этого архива.
	UploadTarDir []string `json:"upload_tar"`
	// UploadStructure задаёт список директорий, структура которых будет воссоздана на YT. Внутри джоба
	// будут созданы все вложенные директории и симлинки.
	//
	// Файлы внутри этих директорий не будут загружены.
	// Target-ы симлинков не будут загружены автоматически.
	// Обход директорий не проходит по симлинкам.
	UploadStructure []string `json:"upload_structure"`

	// StdoutFile задаёт имя файла, куда будет сохранён stdout процесса.
	StdoutFile string `json:"stdout_file"`
	// StderrFile задаёт имя файла, куда будет сохранён stderr процесса.
	StderrFile string `json:"stderr_file"`

	// Outputs задаёт список директорий и файлов, которые будут скачаны с YT после завершения работы `ytexec`.
	// Сохраняется всё содержимое директорий, вместе с файлами и симлинками, но обход не проходит по симлинкам.
	Outputs []string `json:"outputs"`
	// YTOutputs задаёт список директорий и файлов, которые будут сохранены на YT после завершения работы `ytexec`.
	// Сохраняется всё содержимое директорий, вместе с файлами и симлинками, но обход не проходит по симлинкам.
	YTOutputs []string `json:"yt_outputs"`

	// CoredumpDir задаёт директорию, куда будут сохранены `core` файлы. Эта директория будет расположена на жёстком диске.
	// Содержимое директории будет сохранено в YT, использую отдельный формат для хранения sparse файлов.
	CoredumpDir string `json:"coredump_dir"`

	// Ext4Dirs задаёт список директорий, которые будут расположены на ext4 файловой системе.
	Ext4Dirs []string `json:"ext4_dirs"`

	// Download задаёт конфиг для утилиты ytrecipe-tool download.
	//
	// Каждая запись в map задаёт правило prefix -> replace.
	// ytrecipe-tool download будет скачивать те файлы, путь до которых начинается с prefix.
	// Файл prefix/a/b/c будет скачан в output/replace/a/b/c.
	Download map[string]string `json:"download"`
}

func (c *Config) Validate() []error {
	var errs []error
	onError := func(msg string) {
		errs = append(errs, errors.New(msg))
	}

	isInside := func(obj, dir string) bool {
		obj = filepath.Clean(obj)
		dir = filepath.Clean(dir)

		return strings.HasPrefix(obj, dir+string([]rune{filepath.Separator}))
	}

	if c.CoredumpDir == "" {
		onError(`"coredump_dir" is empty`)
	}

	for _, file := range c.UploadFile {
		for _, tarDir := range c.UploadTarDir {
			if isInside(file, tarDir) {
				onError(fmt.Sprintf("file %q is located inside tar dir %q", file, tarDir))
			}
		}
	}

	for _, outerTarDir := range c.UploadTarDir {
		for _, tarDir := range c.UploadTarDir {
			if isInside(outerTarDir, tarDir) {
				onError(fmt.Sprintf("tar dir %q is located inside tar dir %q", outerTarDir, tarDir))
			}
		}
	}

	return errs
}
