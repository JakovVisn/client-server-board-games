#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "virus_war_game.h"

namespace py = pybind11;

PYBIND11_MODULE(viruswar_sim, m) {
    m.doc() = "VirusWar game simulator — C++ core exposed via pybind11";

    py::enum_<AbstractGame::TurnResult>(m, "TurnResult")
        .value("Ongoing",      AbstractGame::TurnResult::Ongoing)
        .value("TurnComplete", AbstractGame::TurnResult::TurnComplete)
        .value("GameOver",     AbstractGame::TurnResult::GameOver);

    // Bad arguments raise ValueError instead of reading out of bounds in C++
    py::class_<VirusWarGame>(m, "VirusWarGame")
        .def(py::init([](int w, int h) {
                 if (!VirusWarGame().isValidBoardSize(w, h))
                     throw py::value_error("invalid board size");
                 return VirusWarGame(w, h);
             }),
             py::arg("width") = VirusWarGame::kDefaultWidth,
             py::arg("height") = VirusWarGame::kDefaultHeight)
        .def("copy",               [](const VirusWarGame& g) { return VirusWarGame(g); })
        .def("current_player",     &VirusWarGame::currentPlayer)
        .def("is_valid_move",      &VirusWarGame::isValidMove)
        .def("apply_move",         &VirusWarGame::applyMove)
        .def("moves_remaining",    &VirusWarGame::movesRemaining)
        .def("turn_result",        &VirusWarGame::turnResult)
        .def("advance_turn",       &VirusWarGame::advanceTurn)
        .def("serialize",          &VirusWarGame::serializeForCurrentPlayer)
        .def("serialize_opponent", &VirusWarGame::serializeForOpponent)
        .def("width",              &VirusWarGame::boardWidth)
        .def("height",             &VirusWarGame::boardHeight)
        .def("init_from_board", [](VirusWarGame& g, const std::string& board, int movesRemaining) {
                 if (static_cast<int>(board.size()) != g.boardWidth() * g.boardHeight())
                     throw py::value_error("board length must be width * height");
                 if (movesRemaining < 1 || movesRemaining > 3)
                     throw py::value_error("moves_remaining must be 1..3");
                 g.initFromBoard(board, movesRemaining);
             }, py::arg("board"), py::arg("moves_remaining") = 3)
        .def("valid_moves", [](const VirusWarGame& g) {
            std::vector<std::pair<int,int>> moves;
            for (int y = 0; y < g.boardHeight(); ++y)
                for (int x = 0; x < g.boardWidth(); ++x)
                    if (g.isValidMove(x, y))
                        moves.emplace_back(x, y);
            return moves;
        });

    // board: serialized from the player to move's view, as sent by the server
    m.def("valid_action_mask", [](const std::string& board, int w, int h) {
        if (w <= 0 || h <= 0 || static_cast<int>(board.size()) != w * h)
            throw py::value_error("board length must be w * h, with w and h positive");
        const VirusWarField field = VirusWarField::fromBoard(board, w, h);
        std::vector<bool> mask(w * h);
        for (int i = 0; i < w * h; ++i)
            mask[i] = field.isValidMove(i % w, i / w);
        return mask;
    }, py::arg("board"), py::arg("w"), py::arg("h"));
}
