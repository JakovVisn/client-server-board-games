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

    py::class_<VirusWarGame>(m, "VirusWarGame")
        .def(py::init<int, int>(),
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
        .def("init_from_board", &VirusWarGame::initFromBoard, py::arg("board"))
        .def("valid_moves", [](const VirusWarGame& g) {
            std::vector<std::pair<int,int>> moves;
            for (int y = 0; y < g.boardHeight(); ++y)
                for (int x = 0; x < g.boardWidth(); ++x)
                    if (g.isValidMove(x, y))
                        moves.emplace_back(x, y);
            return moves;
        });

    // Free function: compute action mask from a serialized board string.
    // Drop-in replacement for game_logic.valid_action_mask(board, w, h).
    m.def("valid_action_mask", [](const std::string& board, int w, int h) {
        VirusWarField field(w, h);
        for (int i = 0; i < static_cast<int>(board.size()); ++i) {
            VirusWarField::Cell c;
            switch (board[i]) {
            case '1': c = VirusWarField::Cell::X;      break;
            case '2': c = VirusWarField::Cell::O;      break;
            case '3': c = VirusWarField::Cell::CatchX; break;
            case '4': c = VirusWarField::Cell::CatchO; break;
            default:  c = VirusWarField::Cell::Clear;  break;
            }
            field.setCell(i % w, i / w, c);
        }
        std::vector<bool> mask(w * h);
        for (int i = 0; i < w * h; ++i)
            mask[i] = field.isValidMove(i % w, i / w);
        return mask;
    }, py::arg("board"), py::arg("w"), py::arg("h"));
}
