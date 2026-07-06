#include "game.h"
#include "question.h"
#include "question_list.h"
#include "question_bank.h"
#include "player.h"
#include "rank.h"
#include "ui.h"
#include "utils.h"

#include <iostream>
#include <string>

// ========================================================================
// Подготовка списка вопросов: заполнить из банка, перемешать, потом
// стабильно отсортировать по номеру раунда. В итоге раунды идут по порядку
// (1, 2, 3), а внутри каждого раунда вопросы в случайном порядке.
// ========================================================================
static void prepareQuestions(QuestionList& list) {
    fillQuestionBank(list);
    qlShuffle(list);

    // Bubble sort по полю round. Bubble стабильна - при равных раундах
    // не сбивает только что перемешанный порядок внутри раунда.
    int n = qlSize(list);
    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < n - i - 1; j++) {
            if (qlAt(list, j).round > qlAt(list, j + 1).round) {
                Question tmp = qlAt(list, j);
                qlAt(list, j) = qlAt(list, j + 1);
                qlAt(list, j + 1) = tmp;
            }
        }
    }
}

// Считаем максимально возможный счёт - сумма всех весов.
// Нужно, чтобы ранг считался в процентах от максимума.
static int calcMaxScore(QuestionList& list) {
    int total = 0;
    for (int i = 0; i < qlSize(list); i++) {
        total = total + qlAt(list, i).weight;
    }
    return total;
}

// ========================================================================
// Опрос игрока для разных типов вопросов.
// Каждая функция печатает варианты, читает ввод и возвращает true,
// если игрок ответил правильно.
// ========================================================================

static bool askMcq(const Question& q) {
    std::cout << CLR_BOLD << "  Что означает: \""
              << CLR_CYAN << q.term << CLR_RESET << CLR_BOLD << "\" ?\n" << CLR_RESET;
    if (!q.hint.empty()) {
        std::cout << "  " << CLR_DIM << "(" << q.hint << ")" << CLR_RESET << "\n";
    }
    std::cout << "\n";
    for (int i = 0; i < 4; i++) {
        std::cout << "    " << CLR_GREEN << (i + 1) << ")" << CLR_RESET
                  << " " << q.options[i] << "\n";
    }
    std::cout << "\n  " << CLR_CYAN << "Твой ответ (1-4): " << CLR_RESET;
    int choice = utilsReadInt();
    int userIndex = -1;
    if (choice >= 1 && choice <= 4) {
        userIndex = choice - 1;
    }
    return userIndex == q.correctIndex;
}

static bool askFillIn(const Question& q) {
    std::cout << CLR_BOLD << "  Термин: " << CLR_CYAN << q.term
              << CLR_RESET << "\n";
    if (!q.hint.empty()) {
        std::cout << "  " << CLR_DIM << q.hint << CLR_RESET << "\n";
    }
    std::cout << "\n  " << CLR_CYAN << "Твой ответ: " << CLR_RESET;

    std::string raw = utilsReadLine();
    std::string answer = utilsToLower(utilsTrim(raw));

    std::string correct1 = utilsToLower(utilsTrim(q.correctAnswer));
    std::string correct2 = utilsToLower(utilsTrim(q.acceptedAlt));

    if (!correct1.empty() && answer == correct1) return true;
    if (!correct2.empty() && answer == correct2) return true;
    return false;
}

static bool askTrueFalse(const Question& q) {
    std::cout << CLR_BOLD << "  " << q.term << CLR_RESET << "\n";
    if (!q.hint.empty()) {
        std::cout << "  " << CLR_DIM << q.hint << CLR_RESET << "\n";
    }
    std::cout << "\n";
    std::cout << "    " << CLR_GREEN << "1)" << CLR_RESET << " Верно\n";
    std::cout << "    " << CLR_GREEN << "2)" << CLR_RESET << " Неверно\n";
    std::cout << "\n  " << CLR_CYAN << "Твой ответ (1-2): " << CLR_RESET;
    int choice = utilsReadInt();
    bool userBool;
    if (choice == 1) userBool = true;
    else if (choice == 2) userBool = false;
    else return false;          // некорректный ввод считаем ошибкой
    return userBool == q.correctBool;
}

// Текстовое представление правильного ответа - для строки "Правильный ответ: ..."
static std::string correctAnswerText(const Question& q) {
    if (q.type == QT_MCQ) {
        return q.options[q.correctIndex];
    }
    if (q.type == QT_FILL_IN) {
        return q.correctAnswer;
    }
    if (q.type == QT_TRUE_FALSE) {
        return q.correctBool ? std::string("Верно") : std::string("Неверно");
    }
    return "";
}

// Один вопрос: вывод шапки, опрос, реакция на результат.
// Возвращает true, если игрок ответил правильно.
static bool runOneQuestion(const Question& q, int qNum, int qTotal, int lives, int score) {
    uiPrintQuestionHeader(qNum, qTotal, lives, score);

    bool correct = false;
    if (q.type == QT_MCQ)        correct = askMcq(q);
    else if (q.type == QT_FILL_IN)    correct = askFillIn(q);
    else if (q.type == QT_TRUE_FALSE) correct = askTrueFalse(q);

    if (correct) {
        uiPrintCorrect(q.weight);
    } else {
        uiPrintWrong(correctAnswerText(q), q.explanation);
    }
    return correct;
}

// ========================================================================
// Главная функция игры.
// ========================================================================
GameResult runGame(bool trainingMode) {
    GameResult result;
    result.failed = false;
    result.score = 0;
    result.maxScore = 0;
    result.playerName = "";
    result.rankTitle = "";
    result.rankFlavor = "";

    utilsClearScreen();
    uiPrintBanner();

    if (trainingMode) {
        std::cout << CLR_YELLOW
                  << "  Режим тренировки. Жизни не снимаются, рекорды не сохраняются.\n"
                  << CLR_RESET;
    }
    std::cout << "\n" << CLR_BOLD << "Как тебя зовут?" << CLR_RESET << "\n> ";

    std::string name = utilsReadLine();
    name = utilsTrim(name);
    if (name.empty()) {
        name = "Аноним";
    }
    result.playerName = name;

    // Готовим вопросы.
    QuestionList list;
    qlInit(list);
    prepareQuestions(list);
    int total = qlSize(list);
    result.maxScore = calcMaxScore(list);

    // Готовим игрока.
    Player player;
    playerInit(player, name);

    // Идём по списку. Раунды отслеживаем, чтобы показывать заставки.
    int currentRound = 0;

    for (int i = 0; i < total; i++) {
        Question& q = qlAt(list, i);

        // Заставка перед сменой раунда.
        if (q.round != currentRound) {
            currentRound = q.round;
            uiPrintRoundIntro(currentRound);
        }

        bool correct = runOneQuestion(q, i + 1, total, player.lives, player.score);

        if (correct) {
            playerAddCorrect(player, q.weight);
        } else {
            // В тренировочном режиме жизни не снимаем.
            if (!trainingMode) {
                playerAddWrong(player);
            } else {
                player.answeredTotal = player.answeredTotal + 1;
            }
        }

        // Если жизни закончились - выходим из цикла досрочно.
        if (!trainingMode && playerIsDead(player)) {
            uiPrintGameOver();
            utilsPause();
            break;
        }

        utilsPause();
    }

    // Заполняем результат и считаем ранг.
    bool failed = !trainingMode && playerIsDead(player);
    result.failed = failed;
    result.score = player.score;

    Rank r = computeRank(player.score, result.maxScore, failed);
    result.rankTitle = r.title;
    result.rankFlavor = r.flavor;

    // Финальный экран.
    uiPrintFinalCard(name,
                     player.score,
                     result.maxScore,
                     r.title,
                     r.flavor,
                     failed);

    // Дополнительная статистика.
    std::cout << "  " << CLR_GREY
              << "Правильных ответов: " << player.answeredCorrect
              << " из " << player.answeredTotal
              << CLR_RESET << "\n\n";

    qlFree(list);
    return result;
}
