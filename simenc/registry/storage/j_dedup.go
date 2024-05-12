package storage
 
import (
	"archive/tar"
	"compress/gzip"
	"fmt"
	"io"
	"os"
	"path"
	"math"

)

 

 
// TarGZ 将 src（文件或目录）打包成 dst（.tar.gz格式）文件
func TarGZ(dst, src string) (err error) {
	// 清理路径字符串
	src = path.Clean(src)
 
	// 获取文件或目录信息
	fileInfo, err := os.Stat(src)
	if err != nil {
		return fmt.Errorf("获取要打包的文件或目录[%v]信息失败[%v]", src, err)
	}
 
	// 创建目标文件
	file, err := os.Create(dst)
	if err != nil {
		return fmt.Errorf("创建目标文件[%v]失败[%v]", dst, err)
	}
 
	// 执行打包操作
	gzipWriter := gzip.NewWriter(file)
	tarWriter := tar.NewWriter(gzipWriter)
	defer func() {
		// 这里要判断是否关闭成功，如果关闭失败，则 .tar.gz 文件可能不完整
		if twErr := tarWriter.Close(); twErr != nil {
			err = twErr
		}
		if gwErr := gzipWriter.Close(); gwErr != nil {
			err = gwErr
		}
		if fwErr := file.Close(); fwErr != nil {
			err = fwErr
		}
	}()
 
	// 获取要打包的文件或目录的所在位置和名称
	srcBase, srcRelative := path.Split(src)
 
	// 开始打包
	if fileInfo.IsDir() {
		return tarDir(srcBase, srcRelative, tarWriter, fileInfo)
	}
	return tarFile(srcBase, srcRelative, tarWriter, fileInfo)
}
 
// tarDir 将 srcBase 目录下的 srcRelative 子目录写入 tarWriter 中，其中 fileInfo 为 srcRelative 子目录信息
func tarDir(srcBase, srcRelative string, tarWriter *tar.Writer, fileInfo os.FileInfo) (err error) {
	// 获取完整路径
	dirPath := path.Join(srcBase, srcRelative)
 
	// 获取目录下的文件和子目录列表
	entries, err := os.ReadDir(dirPath)
	if err != nil {
		return fmt.Errorf("读取目录[%v]失败[%v]", dirPath, err.Error())
	}
 
	// 开始遍历
	for i, entry := range entries {
		entryInfo, err := entry.Info()
		if err != nil {
			return fmt.Errorf("获取目录[%v]下的第[%v]个条目信息失败[%v]", dirPath, i, err.Error())
		}
		filename := path.Join(srcRelative, entryInfo.Name())
		if entryInfo.IsDir() {
			tarDir(srcBase, filename, tarWriter, entryInfo)
		} else {
			tarFile(srcBase, filename, tarWriter, entryInfo)
		}
	}
 
	// 写入目录信息
	if len(srcRelative) > 0 {
		header, err := tar.FileInfoHeader(fileInfo, "")
		if err != nil {
			return fmt.Errorf("使用目录[%v]信息创建压缩目录标头信息失败[%v]", dirPath, err.Error())
		}
		header.Name = srcRelative
		if err = tarWriter.WriteHeader(header); err != nil {
			return fmt.Errorf("向压缩文件写入目录[%v]标头信息失败[%v]", dirPath, err.Error())
		}
	}
 
	return nil
}
 
// tarFile 将 srcBase 目录下的 srcRelative 文件写入 tarWriter 中，其中 fileInfo 为 srcRelative 文件信息
func tarFile(srcBase, srcRelative string, tarWriter *tar.Writer, fileInfo os.FileInfo) (err error) {
	// 获取完整路径
	filepath := path.Join(srcBase, srcRelative)
 
	// 写入文件标头
	header, err := tar.FileInfoHeader(fileInfo, "")
	if err != nil {
		return fmt.Errorf("使用文件[%v]信息创建压缩文件标头信息失败[%v]", filepath, err.Error())
	}
	header.Name = srcRelative
	if err = tarWriter.WriteHeader(header); err != nil {
		return fmt.Errorf("向压缩文件写入文件[%v]标头信息失败[%v]", filepath, err.Error())
	}
 
	// 写入文件数据
	file, err := os.Open(filepath)
	if err != nil {
		return fmt.Errorf("打开文件[%v]失败[%v]", filepath, err.Error())
	}
	defer file.Close()
	if _, err = io.Copy(tarWriter, file); err != nil {
		return fmt.Errorf("向压缩文件写入文件[%v]数据失败[%v]", filepath, err.Error())
	}
 
	return nil
}
 
// UnTarGZ 将 srcFile 文件（.tar.gz 格式）解压到 dstDir 目录下
func UnTarGZ(dstDir, srcFile string) (err error) {
	// 清理路径字符串
	dstDir = path.Clean(dstDir)
 
	// 打开压缩文件
	file, err := os.Open(srcFile)
	if err != nil {
		return err
	}
	defer file.Close()
 
	// 执行解压操作
	gzipReader, err := gzip.NewReader(file)
	if err != nil {
		return err
	}
	defer gzipReader.Close()
	tarReader := tar.NewReader(gzipReader)
	for header, err := tarReader.Next(); err != io.EOF; header, err = tarReader.Next() {
		if err != nil {
			return err
		}
 
		// 获取文件信息
		fileInfo := header.FileInfo()
 
		// 获取绝对路径
		dstFullPath := path.Join(dstDir, header.Name)
 
		if header.Typeflag == tar.TypeDir {
			// 创建目录
			err := os.MkdirAll(dstFullPath, fileInfo.Mode().Perm())
			if err != nil {
				return err
			}
			// 设置目录权限
			os.Chmod(dstFullPath, fileInfo.Mode().Perm())
			// 设置目录修改时间
			os.Chtimes(dstFullPath, fileInfo.ModTime(), fileInfo.ModTime())
		} else if header.Typeflag == tar.TypeSymlink {
			// 处理软链接
			err := os.Symlink(header.Linkname, dstFullPath)
			if err != nil {
				return fmt.Errorf("创建软链接[%v]失败[%v]", dstFullPath, err.Error())
			}
			// 设置软链接权限
			os.Chmod(dstFullPath, fileInfo.Mode().Perm())
			// 设置软链接修改时间
			os.Chtimes(dstFullPath, fileInfo.ModTime(), fileInfo.ModTime())
		} else {
			// 创建文件所在的目录
			os.MkdirAll(path.Dir(dstFullPath), os.ModePerm)
			// 将 tarReader 中的数据写入文件中
			if err := unTarFile(dstFullPath, tarReader); err != nil {
				return err
			}
			// 设置文件权限
			os.Chmod(dstFullPath, fileInfo.Mode().Perm())
			// 设置文件修改时间
			os.Chtimes(dstFullPath, fileInfo.ModTime(), fileInfo.ModTime())
		}
	}
	return nil
}
 
// unTarFile 从 tarReader 读取解压后的数据并写入 dstFile 文件中
func unTarFile(dstFile string, tarReader *tar.Reader) error {
	file, err := os.Create(dstFile)
	if err != nil {
		return err
	}
	defer file.Close()
 
	_, err = io.Copy(file, tarReader)
	if err != nil {
		return err
	}
	return nil
}


func splitFileChunk(srcFile , destfpath string)error{
	var chunkSize int64 = 512 * 1024
	fileInfo, err := os.Stat(srcFile)
	if err != nil {
		fmt.Printf("spliterr : %v",err)
		return err
	}

	num := math.Ceil(float64(fileInfo.Size() ) / float64(chunkSize))

	fi, err := os.OpenFile(srcFile, os.O_RDONLY, os.ModePerm)
	defer  fi.Close()
	if err != nil {
		fmt.Printf("spliterr : %v",err)
		return err
	}

	b := make([]byte, chunkSize)
	var i int64 = 1
	for ; i <= int64(num); i++ {
		fi.Seek((i-1)*chunkSize, 0)
		if len(b) > int(fileInfo.Size()-(i-1)*chunkSize) {
			b = make([]byte, fileInfo.Size()-(i-1)*chunkSize)
		}
		fi.Read(b)
		ofile := fmt.Sprintf("%s/%d",destfpath , i)
		f, err := os.OpenFile(ofile, os.O_CREATE|os.O_WRONLY, os.ModePerm)
		if err != nil {
			fmt.Printf("spliterr : %v",err)
			return err
		}
		f.Write(b)
		f.Close()
	}
	return nil

}


