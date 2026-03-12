# Git 명령어 모음

## 기본 작업

### 상태 확인
```powershell
git status
```
현재 저장소의 변경 상태를 확인합니다.

### 변경사항 보기
```powershell
git diff
```
모든 파일의 변경사항을 봅니다.

```powershell
git diff test.md
```
특정 파일의 변경사항을 봅니다.

---

## 커밋 작업

### 파일 스테이징
```powershell
git add test.md
```
특정 파일을 스테이징합니다.

```powershell
git add .
```
모든 변경된 파일을 스테이징합니다.

### 커밋
```powershell
git commit -m "커밋 메시지"
```
스테이징된 파일을 커밋합니다.

### 한 번에 처리 (Add + Commit + Push)
```powershell
git add . && git commit -m "메시지" && git push
```

---

## 원격 저장소

### 푸시 (업로드)
```powershell
git push
```
로컬 커밋을 원격 저장소에 업로드합니다.

```powershell
git push origin main
```
특정 브랜치에 푸시합니다.

### 풀 (다운로드)
```powershell
git pull
```
원격 저장소의 변경사항을 다운로드합니다.

### 페치 (변경사항 확인만)
```powershell
git fetch
```
원격 저장소의 변경사항을 확인하기만 합니다.

---

## 로그 및 히스토리

### 커밋 로그
```powershell
git log
```
전체 커밋 히스토리를 봅니다.

```powershell
git log --oneline
```
간단한 형태로 커밋 히스토리를 봅니다.

```powershell
git log -n 5
```
최근 5개의 커밋을 봅니다.

---

## 변경사항 취소

### 파일 변경 취소 (저장 전)
```powershell
git restore test.md
```
특정 파일의 변경사항을 취소합니다.

```powershell
git restore .
```
모든 파일의 변경사항을 취소합니다.

### 스테이징 취소
```powershell
git reset test.md
```
특정 파일의 스테이징을 취소합니다.

```powershell
git reset
```
모든 스테이징을 취소합니다.

### 마지막 커밋 취소
```powershell
git reset --soft HEAD~1
```
마지막 커밋을 취소하지만 변경사항은 유지합니다.

```powershell
git reset --hard HEAD~1
```
마지막 커밋을 완전히 취소합니다 (위험!).

---

## 브랜치

### 브랜치 목록
```powershell
git branch
```
로컬 브랜치 목록을 봅니다.

```powershell
git branch -a
```
로컬 및 원격 브랜치 모두를 봅니다.

### 브랜치 생성
```powershell
git branch 브랜치명
```
새로운 브랜치를 생성합니다.

### 브랜치 전환
```powershell
git checkout 브랜치명
```
다른 브랜치로 전환합니다.

### 브랜치 삭제
```powershell
git branch -d 브랜치명
```
브랜치를 삭제합니다.

---

## 유용한 팁

### Git 설정 확인
```powershell
git config --list
```
현재 Git 설정을 확인합니다.

### 마지막 명령어 확인
```powershell
git log -1 --oneline
```
방금 한 커밋을 확인합니다.

### 저장소 초기화
```powershell
git init
```
현재 디렉토리를 Git 저장소로 초기화합니다.

### 특정 원격 저장소 추가
```powershell
git remote add origin https://github.com/user/repo.git
```
원격 저장소를 추가합니다.
